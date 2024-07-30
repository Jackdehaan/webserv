#include "../include/Webserv.hpp"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

volatile sig_atomic_t	interrupted;

void Server::setEnv(char **&env, int index)
{
	char	**savedEnv;
	int		existingEnvCount;
	
	std::vector<std::string> addedEnv;
	std::string currMethod;
	savedEnv = env;
	// Determine the current method
	if (getHttpHandler(index)->getRequest()->method == GET)
		currMethod = "GET";
	else if (getHttpHandler(index)->getRequest()->method == POST)
		currMethod = "POST";
	else if (getHttpHandler(index)->getRequest()->method == DELETE)
		currMethod = "DELETE";
	// Add new environment variables
	addedEnv.push_back("REQUEST_METHOD=" + currMethod);
	addedEnv.push_back("QUERY_STRING="
		+ getHttpHandler(index)->getRequest()->requestBody);
	auto contentTypeIt = getHttpHandler(index)->getRequest()->header.find("Content-Type");
	if (contentTypeIt != getHttpHandler(index)->getRequest()->header.end())
		addedEnv.push_back("CONTENT_TYPE=" + contentTypeIt->second);
	auto contentLengthIt = getHttpHandler(index)->getRequest()->header.find("Content-Length");
	if (contentLengthIt != getHttpHandler(index)->getRequest()->header.end())
		addedEnv.push_back("CONTENT_LENGTH=" + contentLengthIt->second);
	addedEnv.push_back("SERVER_NAME=" + getServerName());
	addedEnv.push_back("SERVER_PORT=" + std::to_string(getPort()));
	addedEnv.push_back("SCRIPT_NAME="
		+ getHttpHandler(index)->getRequest()->requestFile);
	addedEnv.push_back("PATH_INFO="
		+ getHttpHandler(index)->getRequest()->requestURL);
	existingEnvCount = 0;
	while (savedEnv[existingEnvCount] != nullptr)
		existingEnvCount++;
	env = new char *[existingEnvCount + addedEnv.size() + 1];
	for (size_t i = 0; i < addedEnv.size(); ++i)
		env[i] = strdup(addedEnv[i].c_str());
	for (int i = 0; i < existingEnvCount; ++i)
		env[addedEnv.size() + i] = strdup(savedEnv[i]);
	env[addedEnv.size() + existingEnvCount] = nullptr;
}

void Server::execute_CGI_script(int *fds, const char *script, char **env, int index)
{
	char	*exec_args[] = {(char *)script, nullptr};

	logger.log(INFO, "Executing CGI script");
	close(fds[0]);
	setEnv(env, index);
	// Redirect both STDOUT and STDERR
	dup2(fds[1], STDOUT_FILENO);
	dup2(fds[1], STDERR_FILENO);
	close(fds[1]);
	if (getHttpHandler(index)->getRequest()->method == POST)
	{
		write(STDIN_FILENO,
			getHttpHandler(index)->getRequest()->requestBody.c_str(),
			getHttpHandler(index)->getRequest()->requestBody.size());
		// close(STDIN_FILENO);  // Close STDIN after writing
	}
	execve(script, exec_args, env);
	// If execve returns, it failed
	perror("execve failed");
	getHttpHandler(index)->getResponse()->status = httpStatusCode::BadRequest;
	exit(EXIT_FAILURE);
}

void Server::cgi(char **env, int index)
{
	pid_t	pid;
	int		fds[2];

	logger.log(DEBUG, "in CGI");
	if (access(getHttpHandler(index)->getRequest()->requestURL.c_str(),
			X_OK) != 0)
	{
		logger.log(ERR, "[403] Script doesn't have executable rights");
		getHttpHandler(index)->getResponse()->status = httpStatusCode::Forbidden;
		throw ForbiddenException();
	}
	if (pipe(fds) == -1)
	{
		logger.log(ERR, "[500] Pipe has failed");
		getHttpHandler(index)->getResponse()->status = httpStatusCode::InternalServerError;
		throw InternalServerErrorException();
	}
	pid = fork();
	if (pid == -1)
	{
		logger.log(ERR, "[500] Fork has failed");
		getHttpHandler(index)->getResponse()->status = httpStatusCode::InternalServerError;
		throw InternalServerErrorException();
	}
	else if (pid == 0)
		execute_CGI_script(fds,
			getHttpHandler(index)->getRequest()->requestURL.c_str(), env, index);
	else
	{
		close(fds[1]);
		getHttpHandler(index)->getResponse()->contentLength = read(fds[0],
				_buffer, 9999);
		_buffer[getHttpHandler(index)->getResponse()->contentLength] = '\0';
		close(fds[0]);
		makeResponse(_buffer, index);
		waitpid(pid, NULL, 0);
	}
	return ;
}

void Server::setFileInServer(int index)
{
	int		file;
	ssize_t	bytesWritten;

	logger.log(DEBUG, "in setFileInServer");
	if (_upload.empty())
	{
		logger.log(ERR,
			"[403] Tried uploading without setting an upload directory");
		getHttpHandler(index)->getResponse()->status = httpStatusCode::Forbidden;
		throw ForbiddenException();
	}
	else
	{
		std::string uploadPath = getUpload();
		if (access(uploadPath.c_str(), F_OK) != 0)
		{
			mkdir(uploadPath.c_str(), 0775);
			logger.log(WARNING, "Could not find upload directory, so made one automatically.");
		}
		std::string fileName = getHttpHandler(index)->getRequest()->file.fileName;
		std::string &fileContent = getHttpHandler(index)->getRequest()->file.fileContent;
		std::string fullPath = uploadPath + "/" + fileName;
		if (fileName.empty())
		{
			logger.log(ERR, "No file has been uploaded");
			getHttpHandler(index)->getResponse()->status = httpStatusCode::Forbidden;
			throw ForbiddenException();
		}
		else if (access(fullPath.c_str(), F_OK) == 0)
		{
			logger.log(WARNING,
				"File with same name already exists and has been overwritten");
		}
		file = open(fullPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (file != -1)
		{
			bytesWritten = write(file, fileContent.c_str(), BUFFERSIZE);
			// close(file);
			logger.log(DEBUG, std::to_string(bytesWritten) + "|"
				+ std::to_string(getHttpHandler(index)->getRequest()->contentLength));
			// if (bytesWritten == getHttpHandler(index)->getRequest()->contentLength)
			// {
			// 	getHttpHandler(index)->getResponse()->status = httpStatusCode::Created;
			// 	makeResponse((char *)PAGE_201, index);
			// 	logger.log(INFO, "Uploaded a file to " + uploadPath + " called "
			// 		+ fileName);
			// }
			// else
			// {
			// 	logger.log(ERR,
			// 		"[500] Failed to write the entire file content: "
			// 		+ fullPath);
			// 	getHttpHandler(index)->getResponse()->status = httpStatusCode::InternalServerError;
			// 	throw InternalServerErrorException();
			// }
		}
		else
		{
			logger.log(ERR, "[500] Failed to create the file: " + fullPath);
			getHttpHandler(index)->getResponse()->status = httpStatusCode::InternalServerError;
			throw InternalServerErrorException();
		}
	}
}

void Server::deleteFileInServer(int index)
{
	int	fileNameSize;

	logger.log(DEBUG, "in deleteFileInServer");
	std::string filePath = getUpload() + "/"
		+ getHttpHandler(index)->getRequest()->file.fileName;
	fileNameSize = getHttpHandler(index)->getRequest()->file.fileName.size();
	if (getUpload().empty())
	{
		logger.log(ERR,
			"[403] No upload location has been set,can't delete file");
		getHttpHandler(index)->getResponse()->status = httpStatusCode::Forbidden;
		throw ForbiddenException();
	}
	else if (filePath.find("../") != std::string::npos)
	{
		logger.log(ERR,
			"[403] You can only stay in the designated upload folder");
		getHttpHandler(index)->getResponse()->status = httpStatusCode::Forbidden;
		throw ForbiddenException();
	}
	else if (access(filePath.c_str(), F_OK) == -1)
	{
		logger.log(ERR,
			"[403] Tried deleting a file or directory that doesn't exist");
		getHttpHandler(index)->getResponse()->status = httpStatusCode::Forbidden;
		throw ForbiddenException();
	}
	else if (checkIfDir(getUpload() + "/"
			+ getHttpHandler(index)->getRequest()->file.fileName))
	{
		if (getHttpHandler(index)->getRequest()->file.fileName[fileNameSize
			- 1] != '/')
		{
			logger.log(ERR,
				"[409] Tried deleting a directory with unvalid syntax  "
				+ filePath);
			getHttpHandler(index)->getResponse()->status = httpStatusCode::Conflict;
			throw ConflictException();
		}
		else if (access(filePath.c_str(), W_OK) == -1)
		{
			logger.log(ERR, "[403] Directory does not have write permissions  "
				+ filePath);
			getHttpHandler(index)->getResponse()->status = httpStatusCode::Forbidden;
			throw ForbiddenException();
		}
		else
		{
			if (remove(filePath.c_str()) == 0)
			{
				logger.log(INFO,
					"[204] Succesfully deleted the file located at "
					+ filePath);
				getHttpHandler(index)->getResponse()->status = httpStatusCode::NoContent;
				throw NoContentException();
			}
			else
			{
				logger.log(ERR, "[500] Could not delete the file located at "
					+ filePath);
				getHttpHandler(index)->getResponse()->status = httpStatusCode::InternalServerError;
				throw InternalServerErrorException();
			}
		}
	}
	if (remove(filePath.c_str()) == 0)
	{
		logger.log(INFO, "[202] Succesfully deleted the file located at "
			+ filePath);
		getHttpHandler(index)->getResponse()->status = httpStatusCode::Accepted;
		throw AcceptedException();
	}
	else
	{
		logger.log(ERR, "[500] Could not delete the file located at "
			+ filePath);
		getHttpHandler(index)->getResponse()->status = httpStatusCode::InternalServerError;
		throw InternalServerErrorException();
	}
}

void	handleSigInt(int signal)
{
	if (signal == SIGINT)
	{
		logger.log(ERR, "closed Webserv with SIGINT");
		interrupted = 1;
		exit(EXIT_FAILURE);
	}
}

void Webserv::serverActions(int client_socket, request_t request,
	response_t response, int index)
{
	std::cout << "komt hier" << std::endl;
	try
	{
		_servers[0].getHttpHandler(index)->handleRequest(_servers[0], &request,
			&response);
		if (_servers[0].getHttpHandler(index)->getReturnAutoIndex())
			_servers[0].makeResponse((char *)_servers[0].returnAutoIndex(_servers[0].getHttpHandler(index)->getRequest()->requestURL).c_str(), index);
		else if (_servers[0].getHttpHandler(index)->getRequest()->method == DELETE)
			_servers[0].deleteFileInServer(index);
		else if (_servers[0].getHttpHandler(index)->getCgi())
			_servers[0].cgi(_environmentVariables, index);
		else if (_servers[0].getHttpHandler(index)->getRedirect())
			_servers[0].makeResponseForRedirect(index);
		else if (_servers[0].getHttpHandler(index)->getRequest()->file.fileExists)
			_servers[0].setFileInServer(index);
		else
			_servers[0].readFile(index);
	}
	catch (const HttpException &e)
	{
		if (_servers[0].getHttpHandler(index)->getResponse()->status == httpStatusCode::NotFound
			&& !_servers[0].getError404().empty())
		{
			_servers[0].getHttpHandler(index)->getRequest()->requestURL = _servers[0].getRoot()
				+ _servers[0].getError404();
			_servers[0].readFile(index);
		}
		else
			_servers[0].makeResponse(e.getPageContent(), index);
	}
	logger.log(RESPONSE, _servers[0].getResponse());
	if (send(client_socket, _servers[0].getResponse().c_str(),
			strlen(_servers[0].getResponse().c_str()), 0) == -1)
		logger.log(ERR, "[500] Failed to send response to client, send()");
	close(client_socket);
}
void Server::clientConnectionFailed(int client_socket, int index)
{
	logger.log(ERR, "[500] Error in accept()");
	makeResponse((char *)PAGE_500, index);
	if (send(client_socket, getResponse().c_str(),
			strlen(getResponse().c_str()), 0) == -1)
		logger.log(ERR, "[500] Failed to send response to client,send()");
}

int	make_socket_non_blocking(int sfd)
{
	int	flags;

	flags = fcntl(sfd, F_GETFL, 0);
	if (flags == -1)
	{
		perror("fcntl");
		return (-1);
	}
	flags |= O_NONBLOCK;
	if (fcntl(sfd, F_SETFL, flags) == -1)
	{
		perror("fcntl");
		return (-1);
	}
	return (0);
}

int Webserv::execute(void)
{
	int					client_socket;
	char				buffer[BUFFERSIZE];
	ssize_t				read_count;
	socklen_t			addrlen;
	request_t			request[MAX_EVENTS];
	response_t			response[MAX_EVENTS];
	int					eventCount;
	struct epoll_event	eventConfig;
	struct epoll_event	eventList[MAX_EVENTS];
	int					client_tmp;

	signal(SIGINT, handleSigInt);
	addrlen = sizeof(_servers[0].getAddress());
	_servers[0].setServer(_epollFd);
	logger.log(INFO, "Server " + _servers[0].getServerName()
		+ " started on port " + _servers[0].getPortString());
	interrupted = 0;
	while (!interrupted)
	{
		eventCount = epoll_wait(_epollFd, eventList, MAX_EVENTS, 10);
		for (int i = 0; i < eventCount; ++i)
		{
			if (eventList[i].data.fd == _servers[0].getServerFd())
			{
				resetRequestResponse(request[i], response[i]);
				client_socket = accept(_servers[0].getSocketFD(),
						(struct sockaddr *)_servers[0].getAddress(), &addrlen);
				if (client_socket == -1)
				{
					_servers[0].clientConnectionFailed(client_socket, i);
					break ;
				}
				if (make_socket_non_blocking(client_tmp) == -1)
				{
					close(client_tmp);
					continue ;
				}
				eventConfig.events = EPOLLIN | EPOLLET;
				eventConfig.data.fd = client_socket;
				if (epoll_ctl(_epollFd, EPOLL_CTL_ADD, client_socket,
						&eventConfig))
				{
					std::cout << "Connection with epoll_ctl fails!" << std::endl;
					close(client_socket);
				}
				std::cout << "komt hier wel" << std::endl;
			}
			else
			{
				client_tmp = eventList[i].data.fd;
				if (eventList[i].events & EPOLLIN)
				{
					read_count = read(client_tmp, buffer, BUFFERSIZE - 1);
					buffer[read_count] = '\0';
					std::cout << read_count << std::endl;
					if (read_count == -1)
					{
						logger.log(ERR, "Read of client socket failed");
						_servers[0].getHttpHandler(i)->getResponse()->status = httpStatusCode::InternalServerError;
						_servers[0].makeResponse(getHttpStatusHTML(_servers[0].getHttpHandler(i)->getResponse()->status), i);
						close(client_tmp);
					}
					else if (read_count == 0)
					{
						close(client_tmp);
					}
					else
					{
						eventConfig.events = EPOLLOUT | EPOLLET;
						eventConfig.data.fd = client_tmp;
						std::cout << "is nu readen" << std::endl;
						if (epoll_ctl(_epollFd, EPOLL_CTL_MOD, client_tmp,
								&eventConfig))
						{
							std::cout << "Modify does not work" << std::endl;
							close(client_tmp);
						}
					}
				}
				else if (eventList[i].events & EPOLLOUT)
				{
					logger.log(DEBUG,
						"Amount of bytes read from original request: "
						+ std::to_string(read_count));
					parse_request(&request[i], std::string(buffer, read_count));
					serverActions(client_socket, request[i], response[i], i);
					eventConfig.events = EPOLLIN | EPOLLET;
					eventConfig.data.fd = client_tmp;
					if (epoll_ctl(_epollFd, EPOLL_CTL_MOD, client_tmp,
							&eventConfig) == -1)
					{
						close(client_tmp);
					}
				}
			}
		}
	}
	close(_servers[0].getSocketFD());
	logger.log(INFO, "Server shut down");
	return (0);
}
