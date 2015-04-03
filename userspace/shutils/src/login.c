/*
	Glidix Shell Utilities

	Copyright (c) 2014-2015, Madd Games.
	All rights reserved.
	
	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:
	
	* Redistributions of source code must retain the above copyright notice, this
	  list of conditions and the following disclaimer.
	
	* Redistributions in binary form must reproduce the above copyright notice,
	  this list of conditions and the following disclaimer in the documentation
	  and/or other materials provided with the distribution.
	
	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
	AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
	IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
	DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
	FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
	DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
	SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
	CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
	OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
	OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdio.h>
#include <pwd.h>
#include <termios.h>
#include <stdlib.h>
#include <unistd.h>

char username[128];
char password[128];
char passcrypt[128];
char shname[128];

void getline(char *buffer)
{
	ssize_t count = read(0, buffer, 128);
	if (count == -1)
	{
		perror("read");
		exit(1);
	};

	if (count > 127)
	{
		fprintf(stderr, "input too long\n");
		exit(1);
	};

	buffer[count-1] = 0;
};

int nextShadowEntry(FILE *fp)
{
	char *put = shname;
	while (1)
	{
		int c = fgetc(fp);
		if (c == EOF)
		{
			return -1;
		};

		if (c == ':')
		{
			*put = 0;
			break;
		};

		*put++ = (char) c;
	};
	*put = 0;

	put = passcrypt;
	while (1)
	{
		int c = fgetc(fp);
		if (c == EOF)
		{
			return -1;
		};

		if (c == ':')
		{
			*put = 0;
			break;
		};

		*put++ = (char) c;
	};
	*put = 0;

	while (1)
	{
		int c = fgetc(fp);
		if (c == '\n')
		{
			break;
		};

		if (c == EOF)
		{
			return -1;
		};
	};

	return 0;
};

int findPassword(const char *username)
{
	FILE *fp = fopen("/etc/shadow", "r");
	if (fp == NULL)
	{
		perror("open /etc/shadow");
		exit(1);
	};

	while (nextShadowEntry(fp) != -1)
	{
		if (strcmp(shname, username) == 0)
		{
			fclose(fp);
			return 0;
		};
	};

	fclose(fp);
	return -1;
};

int main(int argc, char *argv[])
{
	if ((geteuid() != 0) || (getuid() != 0))
	{
		fprintf(stderr, "%s: you must be root\n", argv[0]);
		return 1;
	};

	while (1)
	{
		printf("Username: "); fflush(stdout); getline(username);

		struct termios tc;
		tcgetattr(0, &tc);
		tc.c_lflag &= ~(ECHO);
		tcsetattr(0, TCSANOW, &tc);

		printf("Password: "); fflush(stdout); getline(password);

		tc.c_lflag |= ECHO;
		tcsetattr(0, TCSANOW, &tc);

		struct passwd *pwd = getpwnam(username);
		if (pwd == NULL)
		{
			fprintf(stderr, "Sorry, please try again.\n");
			continue;
		};

		if (findPassword(username) != 0)
		{
			fprintf(stderr, "Fatal error: this user is not in /etc/shadow\n");
			continue;
		};

		if (strcmp(crypt(password, passcrypt), passcrypt) == 0)
		{
			if (setregid(pwd->pw_gid, pwd->pw_gid) != 0)
			{
				perror("setregid");
				return 1;
			};

			if (setreuid(pwd->pw_uid, pwd->pw_uid) != 0)
			{
				perror("setreuid");
				return 1;
			};

			// at this point, we are logged in as the user.
			if (chdir(pwd->pw_dir) != 0)
			{
				perror("chdir");
				fprintf(stderr, "Using / as home.\n");
				chdir("/");
			};

			printf("Welcome, %s\n", pwd->pw_gecos);
			if (execl(pwd->pw_shell, "-sh", NULL) != 0)
			{
				perror("execl");
				continue;
			};
		};
	};

	return 0;
};