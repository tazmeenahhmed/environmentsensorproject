#include <stdio.h>
#include <stdlib.h>
#include <mysql/mysql.h>  // mySQL library
#include <wiringPi.h>     // wiringPi library
#include <stdint.h>       // integer types
#include <time.h>
#include <string.h>

#define MAX_TIME 85       // maximum time to wait for sensor response
#define DHT11PIN 7        // GPIO pin number where DHT11 is connected

int dht11_val[5] = {0, 0, 0, 0, 0}; // array to store sensor data
int LCDAddr = 0x27;                 // LCD info
int BLEN = 1;
int fd;

// LCD panel functions
void write_word(int data){
    int temp = data;
    if (BLEN == 1)
        temp |= 0x08;
    else
        temp &= 0xF7;
    wiringPiI2CWrite(fd, temp);
}

void send_command(int comm){
    int buf;
    buf = comm & 0xF0;
    buf |= 0x04;
    write_word(buf);
    delay(2);
    buf &= 0xFB;
    write_word(buf);

    buf = (comm & 0x0F) << 4;
    buf |= 0x04;
    write_word(buf);
    delay(2);
    buf &= 0xFB;
    write_word(buf);
}

void send_data(int data){
    int buf;
    buf = data & 0xF0;
    buf |= 0x05;
    write_word(buf);
    delay(2);
    buf &= 0xFB;
    write_word(buf);

    buf = (data & 0x0F) << 4;
    buf |= 0x05;
    write_word(buf);
    delay(2);
    buf &= 0xFB;
    write_word(buf);
}

void init(){
    send_command(0x33);
    delay(5);
    send_command(0x32);
    delay(5);
    send_command(0x28);
    delay(5);
    send_command(0x0C);
    delay(5);
    send_command(0x01);
    wiringPiI2CWrite(fd, 0x08);
}

// reset LCD display
void clear(){
    send_command(0x01);
}

// move cursor on LCD
void write(int x, int y, char data[]){
    int addr, i;
    int tmp;
    if (x < 0)  x = 0;
    if (x > 15) x = 15;
    if (y < 0)  y = 0;
    if (y > 1)  y = 1;

    addr = 0x80 + 0x40 * y + x;
    send_command(addr);

    tmp = strlen(data);
    for (i = 0; i < tmp; i++){
        send_data(data[i]);
    }
}

// read values from the DHT11 sensor
void dht11_read_val(MYSQL *conn)
{
    uint8_t lststate = HIGH;
    uint8_t counter = 0;
    uint8_t j = 0, i;

    // initialize the data array
    for(i = 0; i < 5; i++)
        dht11_val[i] = 0;

    // send start signal to the DHT11 sensor
    pinMode(DHT11PIN, OUTPUT);
    digitalWrite(DHT11PIN, LOW);
    delay(18);
    digitalWrite(DHT11PIN, HIGH);
    delayMicroseconds(40);

    pinMode(DHT11PIN, INPUT);

    for(i = 0; i < MAX_TIME; i++)
    {
        counter = 0;
        while(digitalRead(DHT11PIN) == lststate) {
            counter++;
            delayMicroseconds(1);
            if(counter == 255)
                break;
        }

        lststate = digitalRead(DHT11PIN);

        if(counter == 255)
            break;

        if((i >= 4) && (i % 2 == 0)) {
            dht11_val[j / 8] <<= 1;
            if(counter > 14)  // determining signal
                dht11_val[j / 8] |= 1;
            j++;
        }
    }

    // verify checksum to ensure data integrity
    if((j >= 40) && (dht11_val[4] == ((dht11_val[0] + dht11_val[1] + dht11_val[2] + dht11_val[3]) & 0xFF)))
    {

        char strTemp[10];
        sprintf(strTemp, "%d.%d", dht11_val[2], dht11_val[3]); // grab string temperature

        char strHumi[10];
        sprintf(strHumi, "%d.%d", dht11_val[0], dht11_val[1]); // grab string humidity

        time_t now = time(NULL);
        struct tm *cur_time = localtime(&now);
        char strTime[10];
        sprintf(strTime, "%02d:%02d", cur_time->tm_hour, cur_time->tm_min); // grab string time

        char insert[100];
        sprintf(insert, "insert into day%02d%d%d values ('%s', %s, %s)",
        cur_time->tm_mon+1, cur_time->tm_mday, cur_time->tm_year-100, strTime, strTemp, strHumi);


        if ((dht11_val[0] != 0) && (dht11_val[2]!= 0)){ // prevent data containing zeroes from entering database

            if (mysql_query(conn, insert))
            {
                clear();
                write(0,0, "Error, exiting");
                exit(1);
            }
        }

        // print to LCD for double checking on what is being entered into table
        clear();
        write(0, 0, "Temp: ");
        write(5, 0, strTemp);
        write(0, 1, "Humi: ");
        write(5, 1, strHumi);

    }

    // print an error if checksum fails
    else
    {
        clear();
        write(0, 0, "Invalid Data!");
    }
}

// scrolling text on LCD, for longer messages to fit
void scrollText(const char *message){
    char buffer[17] = {0};                                           // 16 spaces for LCD screen width
    char padded[300] = {0};
    snprintf(padded, sizeof(padded), "%16s%s%16s", "", message, ""); // padding allows for the scroll

    int length = strlen(padded);
    for (int i = 0; i <= length - 16; i++){                          // moves characters left one at a time, starting at first 16
        strncpy(buffer, &padded[i], 16);
        buffer[16] = '\0';

        clear();
        write(0,0, buffer);
        delay(230);                                                  // scroll speed
    }
}

int main()
{

    // LCD panel setup
    fd = wiringPiI2CSetup(LCDAddr);
    init();
    srand(time(NULL));

    // initialize WiringPi library for GPIO control, if initialization fails, exit the program
    if (wiringPiSetup() == -1)
        exit(1);

    MYSQL *conn;
    MYSQL_RES *res;
    MYSQL_ROW row;

    char *server = "localhost";
    char *user = "taz";
    char *password = "raspberry";
    char *database = "projectdb";

    conn = mysql_init(NULL);

    // connect to database
    if (!mysql_real_connect(conn, server, user, password, database, 0, NULL, 0))
    {
        clear();
        write(0, 0, "Error, exiting");
        exit(1);
    }

    // send SQL query
    if (mysql_query(conn, "show tables"))
    {
        fprintf(stderr, "%s\n", mysql_error(conn));
        clear();
        write(0, 0, "Error, exiting");
        exit(1);
    }
    res = mysql_use_result(conn);

    // go through tables
    while ((row = mysql_fetch_row(res)) != NULL){}

    // create new table if new day
    scrollText("Now collecting data:");
    time_t now = time(NULL);
    struct tm *cur_time = localtime(&now);
    char newtable[100];
    sprintf(newtable, "create table if not exists day%02d%d%d (time varchar(50), temperature float, humidity float)",
        cur_time->tm_mon+1, cur_time->tm_mday, cur_time->tm_year-100);

    if (mysql_query(conn, newtable))
    {
        clear();
        write(0, 0, "Error, exiting");
        exit(1);
    }


    // continuous loop to read sensor data at intervals
    while (1)
    {
        // call the function to read values from the DHT11 sensor & write to database
        dht11_read_val(conn);

        // measured in milliseconds, 3000 for 3 seconds, 300000 for five minutes
        delay(30000);
    }

    // close connection
    mysql_free_result(res);
    mysql_close(conn);
    return 0;

}
