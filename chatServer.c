/* server process */

/* include the necessary header files */
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/select.h>

#include "libParseMessage.h"
#include "libMessageQueue.h"

int max(int a, int b)
{
	if (a > b)
		return a;
	return b;
}

typedef struct
{
	char username[MAX_USER_LEN + 1];
	MessageQueue userMessageQueue;
	int registered;
	char *partialMessageBuffer;
	MessageQueue userOutQueue;
} Users;

// goes through list of users and checks if user exists as a username
int checkUserExists(int numusers, Users *userlist, char *user)
{
	for (int j = 0; j < numusers; j++)
	{
		if ((strncmp(userlist[j].username, user, MAX_USER_LEN)) == 0)
		{
			return j;
		}
	}
	return -1;
}

/**
 * send a single message to client
 * sockfd: the socket to read from
 * toClient: a buffer containing a null terminated string with length at most
 * 	     MAX_MESSAGE_LEN-1 characters. We send the message with \n replacing \0
 * 	     for a mximmum message sent of length MAX_MESSAGE_LEN (including \n).
 * return 1, if we have successfully sent the message
 * return 2, if we could not write the message
 */
int sendMessage(int sfd, char *toClient)
{
	char c;
	int offset = 0;
	while (1)
	{
		c = toClient[offset];
		if (c == '\0')
			c = '\n';
		int numSend = send(sfd, &c, 1, 0);
		if (numSend != 1)
			return (2);
		if (c == '\n')
			break;
		offset += 1;
	}
	return (1);
}

/**
 * read a single message from the client.
 * sockfd: the socket to read from
 * fromClient: a buffer of MAX_MESSAGE_LEN characters to place the resulting message
 *             the message is converted from newline to null terminated,
 *             that is the trailing \n is replaced with \0
 * return 1, if we have received a newline terminated string
 * return 2, if the socket closed (read returned 0 characters)
 * return 3, if we have read more bytes than allowed for a message by the protocol
 */
int recvMessage(int sfd, Users *userlist, int index)
{
	// char c;
	// int len= 0;
	// while (1){
	// 	if(len==MAX_MESSAGE_LEN)return(3);

	// 	int numRecv = recv(sfd, &c, 1, 0);
	// 	if(numRecv==0)return(2);
	// 	if(c=='\n')c='\0';
	// 	fromClient[len]=c;
	// 	if(c=='\0')return(1);
	// 	len+=1;
	// }
	char buff[MAX_MESSAGE_LEN];
	int len = recv(sfd, buff, MAX_MESSAGE_LEN, 0);
	if (len == -1)
	{
		return -1;
	}

	char *singlemessage = NULL;
	singlemessage = malloc(MAX_MESSAGE_LEN);
	strncpy(singlemessage, userlist[index].partialMessageBuffer, MAX_MESSAGE_LEN);
	memset(userlist[index].partialMessageBuffer, 0, MAX_MESSAGE_LEN);

	if (len >= MAX_MESSAGE_LEN)
		return (3);
	if (len == 0)
		return (2);

	for (int i = 0; i < len; i++)
	{
		if (buff[i] == '\n' || (buff[i] == '\\' && buff[i + 1] == 'n'))
		{
			buff[i] = '\0';
			strncat(singlemessage, &buff[i], 1);
			enqueue(&userlist[index].userOutQueue, singlemessage);
			i++;
			memset(singlemessage, 0, MAX_MESSAGE_LEN);
		}
		else
		{
			strncat(singlemessage, &buff[i], 1);
		}
	}
	if (buff[len] != '\n')
	{
		// char endstring = '\0';
		// strncat(singlemessage, &endstring, 1);
		strncpy(userlist[index].partialMessageBuffer, singlemessage, MAX_MESSAGE_LEN);
	}
	return (1);
}

int main(int argc, char **argv)
{
	int sockfd;

	if (argc != 2)
	{
		fprintf(stderr, "Usage: %s portNumber\n", argv[0]);
		exit(1);
	}
	int port = atoi(argv[1]);

	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
		perror("socket call failed");
		exit(1);
	}

	struct sockaddr_in server;
	server.sin_family = AF_INET;		 // IPv4 address
	server.sin_addr.s_addr = INADDR_ANY; // Allow use of any interface
	server.sin_port = htons(port);		 // specify port

	if (bind(sockfd, (struct sockaddr *)&server, sizeof(server)) == -1)
	{
		perror("bind call failed");
		exit(1);
	}

	if (listen(sockfd, 5) == -1)
	{
		perror("listen call failed");
		exit(1);
	}

	int fdlist[32];
	int fdcount = 0;
	fdlist[fdcount] = sockfd;
	fdcount++;

	Users *userlist;
	userlist = malloc(sizeof(Users) * 32);

	int numusers = 0;

	for (;;)
	{

		fd_set readfds, writefds, exceptfds;
		FD_ZERO(&readfds);
		FD_ZERO(&writefds);
		FD_ZERO(&exceptfds);

		int fdmax = 0;
		for (int i = 0; i < fdcount; i++)
		{
			if (fdlist[i] > 0)
			{
				FD_SET(fdlist[i], &readfds);
				fdmax = max(fdmax, fdlist[i]);
			}
		}

		struct timeval tv;
		tv.tv_sec = 5;
		tv.tv_usec = 0;

		int numfds;
		if ((numfds = select(fdmax + 1, &readfds, &writefds, &exceptfds, &tv)) > 0)
		{
			for (int i = 0; i < fdcount; i++)
			{
				if (FD_ISSET(fdlist[i], &readfds))
				{
					if (fdlist[i] == sockfd)
					{ /*accept a connection */
						int newsockfd;
						if ((newsockfd = accept(sockfd, NULL, NULL)) == -1)
						{
							perror("accept call failed");
							continue;
						}
						if (fdcount > 32)
						{
							char *errmessage = "closing";
							sendMessage(newsockfd, errmessage);
							close(newsockfd);
							continue;
						}
						fdlist[fdcount] = newsockfd;
						fdcount++;

						MessageQueue queue;
						initQueue(&queue);

						MessageQueue outQueue;
						initQueue(&outQueue);

						userlist[numusers].userMessageQueue = queue;
						userlist[numusers].userOutQueue = outQueue;
						userlist[numusers].username[0] = '\0';
						userlist[numusers].registered = 0;
						userlist[numusers].partialMessageBuffer = malloc(MAX_MESSAGE_LEN);
						numusers++;
					}
					else
					{

						char fromClient[MAX_MESSAGE_LEN], toClient[MAX_MESSAGE_LEN];

						int retVal = recvMessage(fdlist[i], userlist, i - 1);
						if (retVal == 1)
						{
							FD_SET(fdlist[i], &writefds);
							// we have a null terminated string from the client
							while (dequeue(&userlist[i - 1].userOutQueue, fromClient))
							{
								char *part[4];
								int numParts = parseMessage(fromClient, part);
								if (numParts == 0)
								{
									strcpy(toClient, "ERROR");
								}
								else if (strcmp(part[0], "list") == 0)
								{
									strcpy(toClient, "users: ");
									for (int j = 0; j < numusers && j <= 10; j++)
									{
										char *space = " ";
										strncat(toClient, userlist[j].username, MAX_USER_LEN);
										strncat(toClient, space, 1);
									}
								}
								else if (strcmp(part[0], "message") == 0)
								{
									char *fromUser = part[1];
									char *toUser = part[2];
									char *message = part[3];

									if ((strcmp(fromUser, userlist[i - 1].username) != 0) || *fromUser == '\0')
									{
										sprintf(toClient, "invalidFromUser:%s", fromUser);
									}
									else if (checkUserExists(numusers, userlist, toUser) == -1 || *toUser == '\0')
									{
										sprintf(toClient, "invalidToUser:%s", toUser);
									}
									else
									{
										int addressToUser = checkUserExists(numusers, userlist, toUser);
										sprintf(toClient, "%s:%s:%s:%s", "message", fromUser, toUser, message);
										if (strlen(message) > MAX_CHAT_MESSAGE_LEN || strlen(toClient) > MAX_MESSAGE_LEN)
										{
											strcpy(toClient, "closing");
										}
										else if (enqueue(&userlist[addressToUser].userMessageQueue, toClient))
										{
											strcpy(toClient, "messageQueued");
										}
										else
										{
											strcpy(toClient, "messageNotQueued");
										}
									}
								}
								else if (strcmp(part[0], "quit") == 0)
								{
									strcpy(toClient, "closing");
								}
								else if (strcmp(part[0], "getMessage") == 0)
								{
									if (dequeue(&userlist[i - 1].userMessageQueue, toClient) == 0)
									{
										strcpy(toClient, "noMessage");
									}
								}
								else if (strcmp(part[0], "register") == 0)
								{
									if (userlist[i - 1].registered == 1)
									{
										strcpy(toClient, "ERROR");
									}
									else if ((checkUserExists(numusers, userlist, part[1]) != -1))
									{
										strcpy(toClient, "userAlreadyRegistered");
									}
									else
									{
										strncpy(userlist[i - 1].username, part[1], MAX_USER_LEN);
										userlist[i - 1].registered = 1;
										strcpy(toClient, "registered");
									}
								}
								if (FD_ISSET(fdlist[i], &writefds))
								{
									sendMessage(fdlist[i], toClient);
								}
								// this is put here to ensure that the user still exits on "closing" even if there is a block in the writefds
								if (strcmp(toClient, "closing") == 0)
								{
									close(fdlist[i]);
									for (int j = i; j < numusers; j++)
									{
										userlist[j - 1] = userlist[j];
									}
									for (int j = i; j < fdcount - 1; j++)
									{
										fdlist[j] = fdlist[j + 1];
									}
									numusers--;
									fdcount--;
								}
							}
						}
					}
				}
			}
		}
	}
	exit(0);
}
