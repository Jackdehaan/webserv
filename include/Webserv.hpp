/* ************************************************************************** */
/*                                                                            */
/*                                                        ::::::::            */
/*   Webserv.hpp                                        :+:    :+:            */
/*                                                     +:+                    */
/*   By: jade-haa <jade-haa@student.42.fr>            +#+                     */
/*                                                   +#+                      */
/*   Created: 2024/06/09 14:51:39 by rfinneru      #+#    #+#                 */
/*   Updated: 2024/06/19 13:33:42 by rfinneru      ########   odam.nl         */
/*                                                                            */
/* ************************************************************************** */

#pragma once

#include "HttpHandler.hpp"
#include "Server.hpp"
#include "Logger.hpp"
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <regex>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>


class 	Logger;
class	Server;
class	Locations;
class	HttpHandler;


class Webserv
{
  protected:
	std::string _response;
	HttpHandler *_handler;
	std::vector<Server> _servers;

  public:
	int execute(void);
	void printParsing(void);
	void setResponse(const std::string &filePath);
	Webserv(std::string fileName);
	~Webserv();
};

size_t	ft_strlen(const char *s);
char	*ft_strjoin(char const *s1, char const *s2);