#ifndef MY_THREAD_H
#define MY_THREAD_H

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
#define BUF_SIZE 512
class my_thread : public QThread
{
    Q_OBJECT
public:
    explicit my_thread(QObject *parent = nullptr);
    ~my_thread()
    {
            if(my_timer != nullptr)
        delete my_timer;
    }
    SOCKET  my_socket_server, my_socket_local;              //本地DNS和外部DNS两个套接字
    SOCKADDR_IN my_server_name, my_client_name;				//外部DNS、本地两个网络套接字地址
    char my_recv_buf[BUF_SIZE];								//接收缓冲区
    int my_len_client;										//请求端（源地址）buffer大小
    int my_Send, my_Recv; 									//发送接收返回信息
    bool flag;
    QTimer *my_timer;
    void on_timeout();
protected:
    void run();
signals:
    void is_done();
public slots:
};


#endif // MY_THREAD_H
