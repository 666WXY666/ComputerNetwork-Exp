#include <QtCore/QCoreApplication>
#include <stdio.h>
#include <stdlib.h>
#include <winsock2.h>
#include <time.h>
#include <string>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <QTimer>
#include <QThread>
#include <QString>
#include <QDateTime>
#include "my_thread.h"
using namespace std;
#pragma comment(lib,"Ws2_32.lib")

#define LOCAL_DNS "127.0.0.1"
#define DEFAULT_EXTERNAL_DNS "10.3.9.5"
#define DNS_PORT 53
#define BUF_SIZE 512
#define LENGTH 65
#define AMOUNT 1000
#define NOTFOUND -1

typedef struct ip_and_domain
{
    string ip;						//IP地址
    string domain;					//域名
}IP_and_domain;

typedef struct id_change
{
    unsigned short old_ID;			//原有ID
    SOCKADDR_IN client;				//请求者套接字地址
}IDchange;

IP_and_domain DNS_table[AMOUNT];	//DNS域名解析表
IDchange ID_transtable[AMOUNT];	    //ID转换表
int ID_count = 0;					//转换表中的条目个数
char Website[LENGTH];				//域名
long seq=0;
int level=0;

void my_thread::run()
{
    //把recvbuf转发至指定的外部DNS服务器
    my_Send = sendto(my_socket_server, my_recv_buf, my_Recv, 0, (SOCKADDR*)&my_server_name, sizeof(my_server_name));
    if (my_Send == SOCKET_ERROR)
    {
        return;
    }
    else if (my_Send == 0)
    {
        return;
    }

    QThread* timethread = new QThread;
    QTimer *timer = new QTimer;
    timer->setInterval(5000);
    timer->setSingleShot(true);
    timer->moveToThread(timethread);
    connect(timer, &QTimer::timeout, this, &my_thread::on_timeout, Qt::DirectConnection);
    connect(timethread, SIGNAL(started()), timer,SLOT(start()));
    timethread->start();

    //接收来自外部DNS服务器的响应报文
    my_Recv = recvfrom(my_socket_server, my_recv_buf, sizeof(my_recv_buf), 0, (SOCKADDR*)&my_client_name, &my_len_client);
    if(flag)
    {
        disconnect(timer, &QTimer::timeout, this, &my_thread::on_timeout);
        if (my_Recv == SOCKET_ERROR)
        {
            return;
        }
        else if (my_Recv == 0)
        {
            return;
        }

        //ID转换
        unsigned short *present_ID = (unsigned short *)malloc(sizeof(unsigned short));
        memcpy(present_ID, my_recv_buf, sizeof(unsigned short));
        int m = ntohs(*present_ID);
        unsigned short old_ID = htons(ID_transtable[m].old_ID);
        memcpy(my_recv_buf, &old_ID, sizeof(unsigned short));

        //从ID转换表中获取发出DNS请求者的信息
        my_client_name = ID_transtable[m].client;

        //根据发出DNS请求者的信息把recvbuf转发至请求者处
        my_Send = sendto(my_socket_local, my_recv_buf, my_Recv, 0, (SOCKADDR*)&my_client_name, sizeof(my_client_name));
        if (my_Send == SOCKET_ERROR)
        {
            return;
        }
        else if (my_Send == 0)
        {
            return;
        }
        free(present_ID);	//释放动态分配的内存
    }
    emit is_done();
}

//获取域名解析表
int get_table(QString path)
{
    cout <<"【"<<QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss").toUtf8().data()<<"】"<<"<"<<seq++<<">"<< "域名解析文件路径为："<<path.toUtf8().data() << endl;
    int i = 0, j, k;
    string table[AMOUNT];

    ifstream infile(path.toUtf8().data(),ios::in);

    if(infile.is_open()==false)
    {
        cout <<"【"<<QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss").toUtf8().data()<<"】"<<"<"<<seq++<<">"<< "打开域名解析文件失败，请检查后重试！（运行目录中不要有中文路径）" << endl;
        return -1;
    }
    else
    {
        while (getline(infile, table[i]))
        {
            i++;
            if (i == AMOUNT)
            {
                cout <<"【"<<QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss").toUtf8().data()<<"】"<<"<"<<seq++<<">"<<"域名表已满"<< endl;
                break;
            }
        }
        for (j = 0; j < i; j++)
        {
            k = table[j].find(' ');			//k为空格的位置，空格之前的为ip，之后的是域名
            DNS_table[j].ip = table[j].substr(0, k);
            DNS_table[j].domain = table[j].substr(k + 1);
        }
        infile.close();						//关闭文件
        cout <<"【"<<QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss").toUtf8().data()<<"】"<<"<"<<seq++<<">"<< "打开域名解析文件成功！" << endl;
        cout <<"【"<<QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss").toUtf8().data()<<"】"<<"<"<<seq++<<">"<< "解析表共有" << i << "条" << endl;	//i为解析表中条目数。打印出来
        return i;
    }
}

//获取DNS请求中的域名
void get_URL(char *recv_buf, int num)		//recvnum收到的字节数
{
    char name[LENGTH];
    int i = 0, j, k = 0;

    memset(Website, 0, LENGTH);			//把website初始化，全0
    memcpy(name, &(recv_buf[6 * sizeof(unsigned short)]), num - 16);	//获取请求报文中的域名，从recvbuf[12]开始连续的num-16个字节放到name里，16是因为前12和后4个不属于网址

    int len = strlen(name);				//==num-16

    //域名转换
    while (i < len) //J表示距离下个点有多少个字符，例如：'ASCII(3)'www'ASCII(5)'baidu'ASCII(3)'com‘\0’ --> www.baidu.com\0
    {
        if (name[i] > 0 && name[i] <= 63)
        {
            for (j = name[i], i++; j > 0; j--, i++, k++)//只要没进这个循环，i就一直是0
            {
                Website[k] = name[i];
            }
        }
        if (name[i] != 0)
        {
            Website[k++] = '.';
        }
    }
    Website[k] = '\0';
}

//判断是否在表中找到DNS请求中的域名，找到返回下标
int Find(char* Website, int num)//num是table中条数
{
    int k = NOTFOUND;//K=-1
    char* domain;

    for (int i = 0; i < num; i++)
    {
        domain = (char *)DNS_table[i].domain.c_str();
        if (strcmp(domain, Website) == 0) //找到
        {
            k = i;
            break;
        }
    }
    return k;
}

//将请求ID转换为新的ID，并将信息写入ID转换表中，把旧id和套接字地址对应起来，以便查找
unsigned short register_new_ID(unsigned short old_ID, SOCKADDR_IN temp)
{
    ID_transtable[ID_count].old_ID = old_ID;
    ID_transtable[ID_count].client = temp;
    ID_count++;
    ID_count = ID_count % AMOUNT;			//ID数量到达AMOUNT后，从0重新开始
    return (unsigned short)(ID_count - 1);	//以表中下标作为新的ID
}

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    char recv_buf[BUF_SIZE], send_buf[BUF_SIZE];
    WSADATA wsaData;
    SOCKET  socket_server, socket_local;				//本地DNS和外部DNS两个套接字
    SOCKADDR_IN server_name, client_name, local_name;	//外部DNS、请求端和本地DNS三个网络套接字地址
    int len_client;										//请求端（源地址）buffer大小
    int iSend, iRecv;									//发送接收返回信息
    char out_DNS[16];
    int num, i;	//域名解析表中条数
    strcpy(out_DNS, DEFAULT_EXTERNAL_DNS);
    QString my_path=QCoreApplication::applicationDirPath();
    QString path=my_path+QString("/dnsrelay.txt");
    if(argc==1)
    {
        cout <<"【"<<QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss").toUtf8().data()<<"】"<<"<"<<seq++<<">"<<"无调试信息输出"<<endl;
        level=0;
    }
    else if(argc==4)
    {
        if(strcmp(argv[1],"-d")==0)
        {
            cout <<"【"<<QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss").toUtf8().data()<<"】"<<"<"<<seq++<<">"<<"调试信息级别1"<<endl;
            strcpy(out_DNS, argv[2]);     //outerdns = 外部dns
            path=QString(QLatin1String(argv[3]));
            level=1;
        }
        else
        {
            cout <<"【"<<QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss").toUtf8().data()<<"】"<<"<"<<seq++<<">"<<"参数错误，请重试！"<<endl;
            return a.exec();
        }
    }
    else if (argc==3)
    {
        if(strcmp(argv[1],"-dd")==0)
        {
            cout <<"【"<<QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss").toUtf8().data()<<"】"<<"<"<<seq++<<">"<<"调试信息级别2"<<endl;
            strcpy(out_DNS, argv[2]);
            level=2;
        }
        else
        {
            cout <<"【"<<QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss").toUtf8().data()<<"】"<<"<"<<seq++<<">"<<"参数错误，请重试！"<<endl;
            return a.exec();
        }
    }
    else
    {
        cout <<"【"<<QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss").toUtf8().data()<<"】"<<"<"<<seq++<<">"<<"参数错误，请重试！"<<endl;
        return a.exec();
    }
    cout <<"【"<<QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss").toUtf8().data()<<"】"<<"<"<<seq++<<">"<<"DNS服务器为："<<out_DNS<<endl;
    num = get_table(path);						//获取域名解析表
    if(num!=-1)
    {
        for (i = 0; i < AMOUNT; i++) //初始化ID转换表
        {
            ID_transtable[i].old_ID = 0;
            memset(&(ID_transtable[i].client), 0, sizeof(SOCKADDR_IN));
        }

        WSAStartup(MAKEWORD(2, 2), &wsaData);			//初始化ws2_32.dll动态链接库

        //创建本地DNS和外部DNS套接字
        socket_server = socket(AF_INET, SOCK_DGRAM, 0);
        socket_local = socket(AF_INET, SOCK_DGRAM, 0);

        //设置本地DNS和外部DNS两个套接字
        local_name.sin_family = AF_INET;
        local_name.sin_port = htons(DNS_PORT);
        local_name.sin_addr.s_addr = inet_addr(LOCAL_DNS);

        server_name.sin_family = AF_INET;
        server_name.sin_port = htons(DNS_PORT);
        server_name.sin_addr.s_addr = inet_addr(out_DNS);

        //绑定本地DNS服务器地址
        if (::bind(socket_local, (SOCKADDR*)&local_name, sizeof(local_name)))
        {
            cout <<"【"<<QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss").toUtf8().data()<<"】"<<"<"<<seq++<<">"<< "连接53接口失败" << endl;
            exit(1);	//跳出程序
        }
        else
            cout <<"【"<<QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss").toUtf8().data()<<"】"<<"<"<<seq++<<">"<< "成功连接53接口" << endl;


        //本地DNS中继服务器的具体操作
        while (1)
        {
            len_client = sizeof(client_name);
            memset(recv_buf, 0, BUF_SIZE);

            //接收DNS请求,recvfrom返回接到的字节数
            iRecv = recvfrom(socket_local, recv_buf, sizeof(recv_buf), 0, (SOCKADDR*)&client_name, &len_client);
            cout << "-----------------------------------------------------------------------------------------------------------------------" << endl;
            if (iRecv == SOCKET_ERROR)
            {
                cout <<endl<<"【"<<QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss").toUtf8().data()<<"】"<<"<"<<seq++<<">" << "包错误: " << WSAGetLastError()<< endl<< endl;
                continue;
            }
            else if (iRecv == 0)
            {
                break;
            }
            else
            {
                get_URL(recv_buf, iRecv);				//获取域名
                int find = Find(Website, num);			//在域名解析表中查找

                if(level==2)
                {
                    cout << endl << "********************"<<"【"<<QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss").toUtf8().data()<<"】"<<"<"<<seq++<<">"<<"收到请求者的包********************" << endl;
                    for (i = 0; i < iRecv; i++)
                    {
                        printf("%.2x ", (unsigned char)recv_buf[i]);
                    }
                    cout << endl;
                }
                if(level==1||level==2)
                    cout << endl<<"【"<<QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss").toUtf8().data()<<"】"<<"<"<<seq++<<">"<< "对应网址：" << Website<< endl;

                //在域名解析表中没有找到
                if (find == NOTFOUND)
                {
                    //ID转换
                    unsigned short *present_ID = (unsigned short *)malloc(sizeof(unsigned short));
                    memcpy(present_ID, recv_buf, sizeof(unsigned short));
                    unsigned short new_ID = htons(register_new_ID(ntohs(*present_ID), client_name));
                    memcpy(recv_buf, &new_ID, sizeof(unsigned short));
                    cout <<endl<<"【"<<QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss").toUtf8().data()<<"】"<<"<"<<seq++<<">"<< "在域名解析表中没有找到！" << endl;
                    if(level==2)
                    {
                        cout<<"【"<<QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss").toUtf8().data()<<"】"<<"<"<<seq++<<">";
                        printf("id转换:%.2x %.2x -->%.2x %.2x \n\n", (*present_ID) % 0x100, (*present_ID) / 0x100, new_ID % 0x100, new_ID / 0x100);
                        cout << "********************"<<"【"<<QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss").toUtf8().data()<<"】"<<"<"<<seq++<<">"<<"id转换后发给服务器的包********************" << endl;
                        for (int i = 0; i < iRecv; i++)
                        {
                            printf("%.2x ", (unsigned char)recv_buf[i]);
                        }
                        cout << endl;
                    }

                    free(present_ID);	//释放动态分配的内存

                    my_thread *thread = new my_thread;
                    thread->my_socket_server = socket_server;
                    thread->my_socket_local = socket_local;
                    thread->my_client_name = client_name;
                    thread->my_server_name = server_name;
                    thread->my_Recv = iRecv;
                    thread->my_len_client = len_client;
                    memcpy(thread->my_recv_buf, recv_buf, sizeof(recv_buf));
                    thread->start();

                    QObject::connect(thread,&my_thread::is_done,[=]()
                            {
                                thread->quit();
                                //thread->wait();
                                thread->deleteLater();
                            }
                            );
                }
                //在域名解析表中找到
                else
                {
                    cout << endl <<"【"<<QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss").toUtf8().data()<<"】"<<"<"<<seq++<<">"<< "在域名解析表中找到!" << endl;

                    //获取请求报文的ID
                    unsigned short *present_ID = (unsigned short *)malloc(sizeof(unsigned short));
                    memcpy(present_ID, recv_buf, sizeof(unsigned short));

                    //转换ID
                    unsigned short new_ID = register_new_ID(ntohs(*present_ID), client_name);

                    //构造响应报文返回
                    memcpy(send_buf, recv_buf, iRecv);						//拷贝请求报文
                    unsigned short a = htons(0x8180);
                    memcpy(&send_buf[2], &a, sizeof(unsigned short));		//修改标志域

                    //修改回答数，在第6字节处
                    if (strcmp(DNS_table[find].ip.c_str(), "0.0.0.0") == 0)
                    {
                        a = htons(0x0000);	//屏蔽功能：回答数为0
                        cout << endl << "////////////////////屏蔽功能:此网址被屏蔽////////////////////" << endl;
                    }
                    else
                    {
                        a = htons(0x0001);	//服务器功能：回答数为1
                        cout << endl << "////////////////////服务器功能:此网址可快速中继////////////////////" << endl;
                    }
                    memcpy(&send_buf[6], &a, sizeof(unsigned short));
                    int len = 0;

                    //构造DNS响应部分
                    char answer[16];
                    unsigned short Name = htons(0xc00c);//C00C(1100000000001100，12正好是头部的长度，其正好指向Queries区域的查询名字字段)
                    memcpy(answer, &Name, sizeof(unsigned short));
                    len += sizeof(unsigned short);

                    unsigned short TypeA = htons(0x0001);//查询类型：1，由域名获得IPv4地址
                    memcpy(answer + len, &TypeA, sizeof(unsigned short));
                    len += sizeof(unsigned short);

                    unsigned short ClassA = htons(0x0001);//对于Internet信息，总是IN（0x0001）
                    memcpy(answer + len, &ClassA, sizeof(unsigned short));
                    len += sizeof(unsigned short);

                    unsigned long timeLive = htonl(0x00000100);//生存时间(TTL)
                    memcpy(answer + len, &timeLive, sizeof(unsigned long));
                    len += sizeof(unsigned long);

                    unsigned short IPLen = htons(0x0004);//资源数据长度
                    memcpy(answer + len, &IPLen, sizeof(unsigned short));
                    len += sizeof(unsigned short);

                    unsigned long IP = (unsigned long)inet_addr(DNS_table[find].ip.c_str());//资源数据，即IP
                    memcpy(answer + len, &IP, sizeof(unsigned long));
                    len += sizeof(unsigned long);
                    len += iRecv;

                    //请求报文和响应部分共同组成DNS响应报文存入sendbuf
                    memcpy(send_buf + iRecv, answer, len);

                    //发送DNS响应报文
                    iSend = sendto(socket_local, send_buf, len, 0, (SOCKADDR*)&client_name, sizeof(client_name));
                    if (iSend == SOCKET_ERROR)
                    {
                        cout << endl <<"【"<<QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss").toUtf8().data()<<"】"<<"<"<<seq++<<">"<< "包错误: " << WSAGetLastError() << endl<< endl;
                        continue;
                    }
                    else if (iSend == 0)
                    {
                        break;
                    }
                    if(level==2)
                    {
                        cout << endl << "********************"<<"【"<<QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss").toUtf8().data()<<"】"<<"<"<<seq++<<">"<<"发送给请求者的包********************" << endl;
                        for (i = 0; i < (iRecv + 16); i++)
                        {
                            printf("%.2x ", (unsigned char)send_buf[i]);
                        }
                        cout << endl;
                    }

                    free(present_ID);		//释放动态分配的内存
                }
                cout << "\n完成!" << endl << endl;
            }
        }
        closesocket(socket_server);	//关闭套接字
        closesocket(socket_local);
        WSACleanup();				//释放ws2_32.dll动态链接库初始化时分配的资源
        return 0;
    }
    else
    {
        return a.exec();
    }
}
