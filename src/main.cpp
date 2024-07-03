/* ************************************************************************** */
/*                                                                            */
/*                                                        ::::::::            */
/*   main.cpp                                           :+:    :+:            */
/*                                                     +:+                    */
/*   By: jade-haa <jade-haa@student.42.fr>            +#+                     */
/*                                                   +#+                      */
/*   Created: 2024/06/09 14:50:29 by rfinneru      #+#    #+#                 */
/*   Updated: 2024/07/03 16:36:39 by rfinneru      ########   odam.nl         */
/*                                                                            */
/* ************************************************************************** */

#include "../include/Webserv.hpp"

int	main(int argc, char **argv, char **env)
{
	if (argc != 2)
		return (1);
	std::string filename = argv[1];
	HttpHandler test;
	Webserv webserv(filename, env);
	// webserv.printParsing();
	webserv.execute();
	return (0);
} // ~HttpHandler;
