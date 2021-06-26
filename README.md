#### 1、编译准备工作

post.html 以及 post.cgi需要一定的权限才可以执行。

1、cd  httpdocs

2、sudo chmod 600 test.html

sudo chmod 600 post.html

sudo chmod +X post.cgi

![1](https://user-images.githubusercontent.com/66712995/123526496-e5348000-d70a-11eb-810b-1c70d7c77a4e.png)


3、cd  ../

4、make

5、 ./myhttp



#### 2、资源

默认端口号是8888，默认是test.html界面，同一路径下还有 post.html资源

#### 3、整体过程图

![2](https://user-images.githubusercontent.com/66712995/123526501-faa9aa00-d70a-11eb-8397-edf7686a723a.png)

![3](https://user-images.githubusercontent.com/66712995/123526505-01382180-d70b-11eb-8b14-3bc9a90ed954.png)

![4](https://user-images.githubusercontent.com/66712995/123526507-08f7c600-d70b-11eb-8252-80a4aa95c24c.png)

![5](https://user-images.githubusercontent.com/66712995/123526521-157c1e80-d70b-11eb-9784-b3c794b98dcc.png)

#### 4、整体框架图

![myhttp](https://user-images.githubusercontent.com/66712995/123526531-1dd45980-d70b-11eb-9f4b-de30af00801a.png)

#### 5、参考

非常感谢

<TCPIP网络编程>-韩-尹圣雨

https://www.cnblogs.com/qiyeboy/p/6296387.html

https://www.jianshu.com/p/18cfd6019296
