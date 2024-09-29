#include "../include/Webserv.hpp"

void Server::readWriteServer(epoll_event &event, HTTPHandler &handler)
{
	int		client_tmp;
	ssize_t	bytes_read;
	char	buffer[BUFFERSIZE];

	try
	{
		client_tmp = event.data.fd;
		if (event.events & EPOLLIN)
		{
			bytes_read = read(client_tmp, buffer, BUFFERSIZE - 1);
			if (bytes_read < 1)
			{
				readFromSocketError(bytes_read, handler, client_tmp);
				return ;
			}
			buffer[bytes_read] = '\0';
			readFromSocketSuccess(handler, buffer, bytes_read);
			setFdReadyForWrite(client_tmp);
		}
		else if (event.events & EPOLLOUT)
		{
			if (!serverActions(handler, client_tmp))
				return ;
			setFdReadyForRead(client_tmp);
		}
	}
	catch (const FavIconException &e)
	{
		sendFavIconResponse(handler, client_tmp);
	}
	catch (const HttpException &e)
	{
		makeResponse(e.getPageContent(), handler);
		sendResponse(handler, client_tmp);
	}
	catch (const std::exception &e)
	{
		logger.log(ERR, "[500] Internal Server Error in readWriteServer");
		makeResponse(PAGE_500, handler);
		sendResponse(handler, client_tmp);
	}
}

int Webserv::handleEvent(struct epoll_event *eventList, const int &event_fd, int idx)
{
	HTTPHandler			*currentHTTPHandler;
	Server				*currentServer;

	currentHTTPHandler = nullptr;
	
	if (interrupted)
		return (1) ;

	logger.log(INFO, "Caught an event on socket: "
		+ std::to_string(event_fd));

	if (!getServerReceivedFirstRequest(event_fd))
	{
		handleFirstRequest(event_fd);
		logger.log(INFO, "Handled first request on: "
			+ std::to_string(event_fd));
		return (0) ;
	}

	currentServer = findServerConnectedToSocket(event_fd);
	if (!currentServer)
	{
		logger.log(ERR, "No server found for socket: "
			+ std::to_string(event_fd));
		return (0) ;
	}
	currentHTTPHandler = currentServer->matchSocketToHandler(event_fd);
	if (currentHTTPHandler)
	{
		if (currentHTTPHandler->getConnectedToCGI() == nullptr)
			currentServer->readWriteServer(eventList[idx], *currentHTTPHandler);
		else
			currentServer->readWriteCGI(event_fd, *currentHTTPHandler);
	}
	else
		logger.log(ERR, "No HTTP handler found for socket: "
			+ std::to_string(event_fd));
	return (1) ;
}

int Webserv::execute(void)
{
	int					client_socket;
	int					eventCount;
	int					serverEvent;
	socklen_t			addrlen;
	struct epoll_event	eventList[MAX_EVENTS];

	initalizeServers(addrlen);

	while (!interrupted)
	{
		try
		{
			checkCGItimeouts();
			eventCount = epoll_wait(_epollFd, eventList, MAX_EVENTS, 1000);
			for (int idx = 0; idx < eventCount && !interrupted; ++idx)
			{
				serverEvent = checkForNewConnection(eventList[idx].data.fd);
				if (serverEvent >= 0)
				{
					if (!initializeConnection(addrlen, client_socket, serverEvent))
						continue ;
				}
				else
				{
					if (!handleEvent(eventList, eventList[idx].data.fd, idx))
						continue ;
				}
			}
		}
		catch (const std::exception &e)
		{
			logger.log(ERR, "Caught an error inside loop: "
				+ std::string(e.what()));
			close(client_socket);
		}
	}
	cleanUpServers();
	logger.log(INFO, "Webserv has been succesfully closed");
	return (0);
}
