#include "my_thread.h"

my_thread::my_thread(QObject *parent) : QThread(parent)
{
    flag=true;
}
void my_thread::on_timeout()
{
    flag=false;
}
