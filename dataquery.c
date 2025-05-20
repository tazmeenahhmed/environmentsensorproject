#include <stdio.h>
#include <stdlib.h>
#include <mysql/mysql.h>
#include <string.h>
#include <time.h>
#include <wiringPi.h>
#include <wiringPiI2C.h>

// function prototypes
void average(MYSQL *conn);
void maximum(MYSQL *conn);
void minimum(MYSQL *conn);
void hottest(MYSQL *conn);
void coldest(MYSQL *conn);
void scrollText(const char *message);
void customChar(void);

int LCDAddr = 0x27;
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
        delay(160);                                                  // scroll speed
    }
}

int main(void)
{
    // LCD panel setup
    fd = wiringPiI2CSetup(LCDAddr);
    init();
    srand(time(NULL));

    // MYSQL setup
    MYSQL *conn;
    char *server = "localhost";
    char *user = "taz";
    char *password = "raspberry";
    char *database = "projectdb";

    // connect to database
    conn = mysql_init(NULL);
    if (!mysql_real_connect(conn, server, user, password, database, 0, NULL, 0))
    {
        fprintf(stderr, "%s\n", mysql_error(conn));
        exit(1);
    }

    // loop main menu until exit is chosen
    int choice = 0;
    while (choice != 6){

        scrollText("Choose from menu: ");

        clear();
        write(0, 0, "1. AVERAGE");
        write(0, 1, "2. MINIMUM");
        sleep(2);

        clear();
        write(0, 0, "3. MAXIMUM");
        write(0, 1, "4. HOTTEST");
        sleep(2);

        clear();
        write(0, 0, "5. COLDEST");
        write(0, 1, "6. EXIT");
        sleep(2);

        clear();
        write(0, 0, "Your choice:");
        scanf("%d", &choice);

        switch (choice){
            case 1:
                average(conn);
                break;
            case 2:
                minimum(conn);
                break;
            case 3:
                maximum(conn);
                break;
            case 4:
                hottest(conn);
                break;
            case 5:
                coldest(conn);
                break;
            case 6:
                clear();
                write(0, 0, "Exited program");
                break;
            default:
                scrollText("Input not recognized");
        }
    }

    // close MYSQL connection
    mysql_close(conn);
    return 0;

}

// find average from a specific data collection day
void average(MYSQL *conn) {

    MYSQL_RES *res;
    MYSQL_ROW row;

    // print out tables to choose from
    scrollText("Select day to find average from:");
    if (mysql_query(conn, "show tables"))
    {
        clear();
        write(0, 0, "Error, exited");
        exit(1);
    }
    res = mysql_store_result(conn);

    while ((row = mysql_fetch_row(res)) != NULL){
        char day[100];
        sprintf(day, "%s", row[0]);
        scrollText(day);
    }

    // send query to collect specific date data + input validation
    char selection[50];
    while (1){

        clear();
        write(0, 0, "Choose a date:");
        scanf("%s", selection);

        char query[100];
        sprintf(query, "select * from %s", selection);

        if (mysql_query(conn, query))
        {
            scrollText("Table doesn't exist, enter again");
            continue;
        }

        break;
    }
    res = mysql_store_result(conn);

    // process the averages
    double avgTemp = 0.0;
    double avgHumi = 0.0;
    int count = 0;
    while ((row = mysql_fetch_row(res)) != NULL){
        avgTemp += atof(row[1]);
        avgHumi += atof(row[2]);
        count++;
    }

    avgTemp /= count;
    avgHumi /= count;

    // print out result to LCD display
    char strHumi[20];
    sprintf(strHumi, "H: %0.1f%%", avgHumi);

    char strTemp[20];
    sprintf(strTemp, "T: %0.1fC", avgTemp);

    clear();
    write(0, 0, strTemp);
    write(0, 1, strHumi);

    // pause program before going back to menu
    sleep(3);
    mysql_free_result(res);

}

// find coldest temperature + lowest humidity from specific day
void minimum(MYSQL *conn) {

    MYSQL_RES *res;
    MYSQL_ROW row;

    // print out tables to choose from
    scrollText("Select day to find minimum from:");
    if (mysql_query(conn, "show tables"))
    {
        clear();
        write(0, 0, "Error, exited");
        exit(1);
    }
    res = mysql_store_result(conn);

    while ((row = mysql_fetch_row(res)) != NULL){
        char day[100];
        sprintf(day, "%s", row[0]);
        scrollText(day);
    }

    // send query to collect specific date data + input validation
    char selection[50];
    while (1){

        clear();
        write(0, 0, "Choose a date:");
        scanf("%s", selection);

        char query[100];
        sprintf(query, "select * from %s", selection);

        if (mysql_query(conn, query))
        {
            scrollText("Table doesn't exist, enter again");
            continue;
        }

        break;
    }
    res = mysql_store_result(conn);

    // find coldest temperature & lowest humidity
    double coldest = 400.0;                     // 400.0 *C since that's not a possible temperature/humidity
    double lowest = 400.0;                      // for day-to-day weather
    char timeTemp[10];
    char timeHumi[10];
    while ((row = mysql_fetch_row(res)) != NULL){
        double temp = atof(row[1]);
        double humi = atof(row[2]);
        if (temp < coldest){
            coldest = temp;
            strncpy(timeTemp, row[0], sizeof(timeTemp) - 1);
            timeTemp[sizeof(timeTemp)-1] = '\0';
        }

        if (humi < lowest){
            lowest = humi;
            strncpy(timeHumi, row[0], sizeof(timeHumi) - 1);
            timeHumi[sizeof(timeHumi)-1] = '\0';
        }

    }

    // print out result to LCD display
    char strHumi[50];
    sprintf(strHumi, "H: %0.1f%% (%s)", lowest, timeHumi);

    char strTemp[50];
    sprintf(strTemp, "T: %0.1fC (%s)", coldest, timeTemp);

    clear();
    write(0, 0, strTemp);
    write(0, 1, strHumi);

    // pause program before going back to menu
    sleep(3);
    mysql_free_result(res);

}

// find hottest temperature + highest humidity from specific day
void maximum(MYSQL *conn) {

    MYSQL_RES *res;
    MYSQL_ROW row;

    // print out tables to choose from
    scrollText("Select a day to find the maximum from:");
    if (mysql_query(conn, "show tables"))
    {
        clear();
        write(0, 0, "Error, exited");
        exit(1);
    }
    res = mysql_store_result(conn);

    while ((row = mysql_fetch_row(res)) != NULL){
        char day[100];
        sprintf(day, "%s", row[0]);
        scrollText(day);
    }

    // send query to collect specific date data + input validation
    char selection[50];
    while (1){

        clear();
        write(0, 0, "Choose a date:");
        scanf("%s", selection);

        char query[100];
        sprintf(query, "select * from %s", selection);

        if (mysql_query(conn, query))
        {
            scrollText("Table doesn't exist, enter again");
            continue;
        }

        break;
    }
    res = mysql_store_result(conn);

    // find hottest temperature & highest humidity
    double hottest = 0.0;
    double highest = 0.0;
    char timeTemp[10];
    char timeHumi[10];
    while ((row = mysql_fetch_row(res)) != NULL){
        double temp = atof(row[1]);
        double humi = atof(row[2]);
        if (temp > hottest){
            hottest = temp;
            strncpy(timeTemp, row[0], sizeof(timeTemp) - 1);
            timeTemp[sizeof(timeTemp)-1] = '\0';
        }

        if (humi > highest){
            highest = humi;
            strncpy(timeHumi, row[0], sizeof(timeHumi) - 1);
            timeHumi[sizeof(timeHumi)-1] = '\0';
        }

    }

    // output results to LCD display
    char strHumi[50];
    sprintf(strHumi, "H: %0.1f%% (%s)", highest, timeHumi);

    char strTemp[50];
    sprintf(strTemp, "T: %0.1fC (%s)", hottest, timeTemp);

    clear();
    write(0, 0, strTemp);
    write(0, 1, strHumi);

    // pause program before going back to menu
    sleep(3);
    mysql_free_result(res);

}

// find hottest day among all collection days
void hottest(MYSQL *conn) {

    // establish variables
    MYSQL_RES *resOuter;
    MYSQL_RES *resInner;
    MYSQL_ROW row;

    double estHottest = 0.0;
    char estTime[10];
    char day[20];

    // query tables
    if (mysql_query(conn, "show tables"))
    {
        clear();
        write(0, 0, "Error, exited");
        exit(1);
    }
    resOuter = mysql_store_result(conn);

    // outer loop through all the tables
    while ((row = mysql_fetch_row(resOuter)) != NULL){

        // store table name while row[0] is still applicable to table
        char ifDay[20];
        strncpy(ifDay, row[0], sizeof(ifDay) - 1);
        ifDay[sizeof(ifDay)-1] = '\0';

        // query data from specific table
        char query[100];
        sprintf(query, "select * from %s", row[0]);

        if (mysql_query(conn, query)){
            clear();
            write(0, 0, "Error, exited");
            exit(1);
        }
        resInner = mysql_store_result(conn);

        // sift through day's data and find hottest moment
        double hottest = 0.0;
        char timeTemp[10];
        while ((row = mysql_fetch_row(resInner)) != NULL){
            double temp = atof(row[1]);
            if (temp > hottest){
                hottest = temp;
                strncpy(timeTemp, row[0], sizeof(timeTemp) - 1);
                timeTemp[sizeof(timeTemp)-1] = '\0';
            }

        }

        // replace values if there is new hottest
        if (hottest > estHottest){
            estHottest = hottest;

            strncpy(estTime, timeTemp, sizeof(estTime) - 1);
            estTime[sizeof(estTime)-1] = '\0';

            strncpy(day, ifDay, sizeof(day) - 1);
        }

    }


    // output results to LCD display
    char bottomLine[50];
    sprintf(bottomLine, "%0.1fC (%s)", estHottest, estTime);

    clear();
    write(0, 0, "Hottest DB day: "); // DB - database, hottest day on database
    write(0, 1, day);
    sleep(3);

    clear();
    write(0, 0, "At temperature: ");
    write(0, 1, bottomLine);
    sleep(3);

    mysql_free_result(resOuter);
    mysql_free_result(resInner);

}

// find coldest day among all collection days
void coldest(MYSQL *conn){

    // establish variables
    MYSQL_RES *resOuter;
    MYSQL_RES *resInner;
    MYSQL_ROW row;

    double estColdest = 400.0; // 400.0 *C to set upper limit since not possible for day to day temperature
    char estTime[10];
    char day[20];

    // query tables
    if (mysql_query(conn, "show tables"))
    {
        clear();
        write(0, 0, "Error, exited");
        exit(1);
    }
    resOuter = mysql_store_result(conn);

    // outer loop through all the tables
    while ((row = mysql_fetch_row(resOuter)) != NULL){

        // store table name while row[0] is still applicable to table
        char ifDay[20];
        strncpy(ifDay, row[0], sizeof(ifDay) - 1);
        ifDay[sizeof(ifDay)-1] = '\0';

        // query data from specific table
        char query[100];
        sprintf(query, "select * from %s", row[0]);

        if (mysql_query(conn, query)){
            clear();
            write(0, 0, "Error, exited");
            exit(1);
        }
        resInner = mysql_store_result(conn);

        // sift through day's data and find coldest moment
        double coldest = 400.0;
        char timeTemp[10];
        while ((row = mysql_fetch_row(resInner)) != NULL){
            double temp = atof(row[1]);
            if (temp < coldest){
                coldest = temp;
                strncpy(timeTemp, row[0], sizeof(timeTemp) - 1);
                timeTemp[sizeof(timeTemp)-1] = '\0';
            }

        }

        // replace values if there is new coldest
        if (coldest < estColdest){
            estColdest = coldest;

            strncpy(estTime, timeTemp, sizeof(estTime) - 1);
            estTime[sizeof(estTime)-1] = '\0';

            strncpy(day, ifDay, sizeof(day) - 1);
        }

    }

    // output results to LCD display
    char bottomLine[50];
    sprintf(bottomLine, "%0.1fC (%s)", estColdest, estTime);

    clear();
    write(0, 0, "Coldest DB day: "); // DB - database, coldest day on database
    write(0, 1, day);
    sleep(3);

    clear();
    write(0, 0, "At temperature: ");
    write(0, 1, bottomLine);
    sleep(3);

    mysql_free_result(resOuter);
    mysql_free_result(resInner);

}

