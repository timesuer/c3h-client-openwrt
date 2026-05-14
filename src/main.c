/*
 * Filename:     main.c
 *
 * Created by:	 liuqun
 * Revised by:   KiritoA, Skape
 * Description:  校园网802.1X客户端程序入口
 *               支持认证后自动DHCP获取IP，
 *               支持后台运行、定时网络检测与自动重拨。
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>

#ifndef WIN32
#include <unistd.h>
#include <sys/wait.h>
#endif

#include "defs.h"
#include "auth.h"
#include "adapter.h"

#define ARG_NUMBER	4
#define ABOUT_INFO_STRING "C3H Client 15.12 (Enhanced)\n"

/* 网络监控参数 */
#define MONITOR_INTERVAL_SEC  (30 * 60)  /* 30分钟检测一次 */
#define PING_TARGET           "www.baidu.com"
#define MAX_RETRY_ATTEMPTS    5          /* 最多重试拨号5次 */

static volatile int g_running = 1;

void signal_interrupted (int signo)
{
    g_running = 0;
    LogOff();
	CloseDevice();
    exit(0);
}

void showUsage()
{
	PRINT("C3H Client (Enhanced)\n"
		"Usage:\n"
		"\tc3h-client [username] [password] [adapter] [reconnect]\n"
		"\t[Username]   Your Username.\n"
		"\t[password]   Your Password.\n"
		"\t[adapter]    Specify ethernet adapter to use.\n"
		"\t             Adapter in Linux is eth0,eth1,...etc\n"
		"\t             Adapter in Windows starts with '\\Device\\NPF_'\n"
		"\t[reconnect]  Times to reconnect after failure. value 0 will disable reconnection feature.\n\n");
}

/**
 * 检测网络连通性
 * 返回: 0=网络正常, 非0=网络异常
 */
static int CheckNetworkConnectivity()
{
	int ret;
	char cmd[128];
	/* ping 3个包，超时5秒，静默模式 */
	snprintf(cmd, sizeof(cmd), "ping -c 3 -W 5 %s > /dev/null 2>&1", PING_TARGET);
	ret = system(cmd);
	/* system() 返回的是 waitpid 的状态，需要用 WEXITSTATUS 提取退出码 */
	if (WIFEXITED(ret)) {
		return WEXITSTATUS(ret);
	}
	return -1;
}

/**
 * 执行一次完整的拨号流程（认证 + DHCP）
 * 返回: 0=成功, 非0=失败
 */
static int DoFullDialup(const char *userName, const char *password, const char *deviceName, int reconnect)
{
	int ret;
	char reconnStr[16];
	snprintf(reconnStr, sizeof(reconnStr), "%d", reconnect);

	/* 重新初始化设备（上次认证可能已关闭） */
	CloseDevice();
	InitDevice(deviceName);

	PRINTMSG("C3H Client: Starting 802.1X authentication...\n");

	/* 802.1X认证 */
	int overheat = 0;
	int retry = 0;
	int success_count = 0;
	int failure_count = 0;
	int reconn = reconnect;
	time_t lastAuthTime;

	do
	{
		lastAuthTime = time(NULL);
		ret = Authentication(userName, password);

		if (ret == ERR_AUTH_MAC_FAILED)
		{
			PRINTERR("C3H Client: Connection Failed(Code:%d).\n", ret);
			break;
		}
		else if (ret == 0 || ret == ERR_AUTH_TIME_LIMIT)
		{
			/* 认证成功后正常断线或时间限制 */
			PRINTMSG("C3H Client: Connection closed.\n");
			/* 认证曾经成功过，返回0表示可以继续监控 */
			CloseDevice();
			return 0;
		}
		else
		{
			PRINTERR("C3H Client: Connection Failed(Code:%d).\n", ret);

			if (ret == ERR_FAILED_AFTER_SUCCESS)
			{
				reconn = reconnect; //重置重连计数
				success_count++;
				retry = 0;
			}
			else
			{
				failure_count++;
			}

			if(reconn == 0)
				break;

			if ((time(NULL) - lastAuthTime) < 20 && ++overheat > 3)
			{
					PRINTMSG("C3H Client: Wait for 20s...\n");
					sleep(20);
					overheat = 0;
			}
			else
				sleep(5);

			retry++;
			PRINTMSG("C3H Client: Reconnecting...[S:%d F:%d R:%d]\n", success_count, failure_count, retry);
		}
	} while (reconn--);

	CloseDevice();
	PRINTMSG("C3H Client: Auth exit.[S:%d F:%d R:%d]\n", success_count, failure_count, retry);
	return ret;
}


/**
 * 函数：main()
 *
 */
int main(int argc, char *argv[])
{
	char *UserName;
	char *Password;
	char *DeviceName;
	char *Reconnect;

	/* 禁用stdout缓冲，确保日志即时输出（用于后台运行时重定向到文件） */
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

#ifndef WIN32
	/* 检查当前是否具有root权限 */
	if (getuid() != 0) {
		PRINT( "抱歉，运行本客户端程序需要root权限\n");
		PRINT( "(RedHat/Fedora下使用su命令切换为root)\n");
		PRINT( "(Ubuntu/Debian下在命令前添加sudo)\n");
		exit(-1);
	}
#endif
	/* 检查命令行参数格式 */
	if (argc != ARG_NUMBER+1) {

		showUsage();
		ListAllAdapters();
		exit(-1);
	}

	UserName = argv[1];
	Password = argv[2];
	DeviceName = argv[3]; // 允许从命令行指定设备名
	Reconnect = argv[4];//重连次数

	int i;

	for (i = 0; i < (int)strlen(Reconnect); i++)
	{
		if (Reconnect[i]<'0' || Reconnect[i]>'9')
		{
			PRINTERR("Invalid reconnect value.\r");
			exit(-1);
		}
	}

	int reconnect = atoi(Reconnect);

	//此时开始按下Ctrl+C可退出程序
	signal(SIGINT, signal_interrupted);
	signal(SIGTERM, signal_interrupted);

	PRINTMSG(ABOUT_INFO_STRING);

	/*
	 * 主循环：
	 * 1. 执行802.1X认证
	 * 2. 认证成功后自动DHCP获取IP（在auth.c的SUCCESS分支中调用RefreshIPAddress完成）
	 * 3. 每30分钟ping检测网络连通性
	 * 4. 网络不通则重新拨号，最多重试5次
	 */
	int dialup_ret;
	int monitor_retry;

	/* 首次拨号 */
	dialup_ret = DoFullDialup(UserName, Password, DeviceName, reconnect);
	if (dialup_ret != 0) {
		PRINTERR("C3H Client: Initial authentication failed (Code:%d). Exiting.\n", dialup_ret);
		return dialup_ret;
	}

	PRINTMSG("C3H Client: Authentication and DHCP completed successfully.\n");
	PRINTMSG("C3H Client: Entering network monitoring mode (interval: %d min)...\n",
			MONITOR_INTERVAL_SEC / 60);

	/* 网络监控循环 */
	while (g_running)
	{
		/* 等待监控间隔 */
		sleep(MONITOR_INTERVAL_SEC);

		if (!g_running) break;

		PRINTMSG("C3H Client: Checking network connectivity...\n");

		if (CheckNetworkConnectivity() == 0)
		{
			PRINTMSG("C3H Client: Network is OK.\n");
			continue;
		}

		PRINTERR("C3H Client: Network check failed! Attempting to re-authenticate...\n");

		/* 网络不通，尝试重新拨号 */
		monitor_retry = 0;
		while (monitor_retry < MAX_RETRY_ATTEMPTS && g_running)
		{
			monitor_retry++;
			PRINTMSG("C3H Client: Re-dial attempt %d/%d...\n", monitor_retry, MAX_RETRY_ATTEMPTS);

			dialup_ret = DoFullDialup(UserName, Password, DeviceName, reconnect);
			if (dialup_ret == 0)
			{
				PRINTMSG("C3H Client: Re-authentication successful!\n");

				/* 等待DHCP完成后再次检查网络 */
				sleep(5);
				if (CheckNetworkConnectivity() == 0)
				{
					PRINTMSG("C3H Client: Network restored.\n");
					break;
				}
				else
				{
					PRINTERR("C3H Client: Auth OK but network still down, retrying...\n");
				}
			}
			else
			{
				PRINTERR("C3H Client: Re-dial failed (Code:%d).\n", dialup_ret);
			}

			/* 重试间隔 */
			if (monitor_retry < MAX_RETRY_ATTEMPTS)
			{
				PRINTMSG("C3H Client: Waiting 30s before next retry...\n");
				sleep(30);
			}
		}

		if (monitor_retry >= MAX_RETRY_ATTEMPTS)
		{
			PRINTERR("C3H Client: All %d re-dial attempts failed. Will check again in %d min.\n",
					MAX_RETRY_ATTEMPTS, MONITOR_INTERVAL_SEC / 60);
		}
	}

	CloseDevice();
	PRINTMSG("C3H Client: Stopped.\n");
	return 0;
}
