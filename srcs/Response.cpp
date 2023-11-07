#include "Response.hpp"


/**************************************************************/
/*                 DEFAULT CON-/DESTRUCTOR                    */
/**************************************************************/

/*
*	Default constructor to set the message status line
*	to the given http status code.
*/
Response::Response( HttpStatusCode httpStatus )
	: _msgBodyLength(0), _msgBody("")
{
	_readHttpStatusCodeDatabase();
	if ( _httpStatusCodeLookup.empty() )
		throw std::runtime_error("Error: http status code database empty");
	
	_msgStatusLine.protocolVersion = HTTP_VERSION;

	_msgStatusLine.statusCode = httpStatus;
	std::map<std::string, std::string>::iterator it = _httpStatusCodeLookup.find(_msgStatusLine.statusCode);
	if ( it != _httpStatusCodeLookup.end() )
		_msgStatusLine.reasonPhrase = it->second;
	else
		_msgStatusLine.reasonPhrase = "";
	
}

/*
*	1) Check preconditions
*	2) Check for redirection
*	3) Check for method and call corresponding handler	
*/
Response::Response( RequestParser request, ServerConfig config )
	: _config(config), _request(request), _msgBodyLength(0), _msgBody("")
{
	_readHttpStatusCodeDatabase();
	if ( _httpStatusCodeLookup.empty() )
		throw std::runtime_error("Error: http status code database empty");

	_msgStatusLine.protocolVersion = HTTP_VERSION;
	_method = request.getMethod();
	_setPaths( reqUri );

	if ( !_checkPreconditions( ) )	// path dependent on configFile!!!
		;
	else if ( _checkRedirection( ) )
		;
	else if ( _method == GET )
		_handleGet();
	else if ( _method == POST )
		_handlePost();
	else if ( _method == DELETE )
		_handleDelete();

}


/* Response::Response( std::map<std::string, std::string> )
	: 
{
	_buildResponse( _paths );
} */

Response::~Response()
{
	
}


/**************************************************************/
/*                    PRIVATE METHODS                         */
/**************************************************************/

/* _setPaths:
*	Sets the paths for the response depending on the request URI and the config file.
*/
void	Response::_setPaths( std::string reqUri )
{
	_paths.requestUri = reqUri;
	_paths.confLocKey = _config.getLocationKey( reqUri );
	_paths.responseUri = _config.getUri( _paths.confLocKey, reqUri );
}

/* _checkPreconditions:
*	Checks if the request meets all preconditions:
*		- path/file exists ⇒ 404 Not Found
*		- right access to path/file (GET->read | ???POST->write??? | DELETE->execute) ⇒ 403 Forbidden
*		- method of request invalid ⇒ 405 Method Not Allowed
*		- HTTP version supported (== 1.1) ⇒ 505 HTTP Version Not Supported
*		- request body size valid (<= confifg.clientMaxBodySize) ⇒ 413 Content Too Large
*		- No or wrong “Host” field in request ⇒ 400 Bad Request
*		- Content length smaller 0 ⇒ 400 Bad Request
*	Returns true if all preconditions are met, false otherwise.
*/
bool	Response::_checkPreconditions()
{
	// check if path/file exists
	if ( access(_paths.responseUri.c_str(), F_OK) != 0 ) {
		_msgStatusLine.statusCode = STATUS_404;
		return false;
	}

	// check if right access to path/file
	if ( (_method == GET && access(_paths.responseUri.c_str(), R_OK) != 0 ) 
		|| (_method == DELETE && access(_paths.responseUri.c_str(), X_OK) != 0 ) ) {
		_msgStatusLine.statusCode = STATUS_403;
		return false;
	}

	// check if method is allowed
	std::vector<httpMethod> methods = _config.getLocations[_paths.confLocKey].getMethods();
	for ( std::vector<httpMethod>::iterator it = methods.begin(); it != methods.end(); it++ ) {
		if ( *it == _method )
			break ;
		else if ( it == _config.allowedMethods.end() ) {
			_msgStatusLine.statusCode = STATUS_405;
			return false;
		}
	}

	// check if HTTP version is supported
	if ( _request.getProtocol() != HTTP_VERSION ) {
		_msgStatusLine.statusCode = STATUS_505;
		return false;
	}

	// check if request body size is valid
	if ( _request.getBodyLength() <= _config.getClientMaxBodySize() ) {	// ask Linus to return ssize_t!!
		_msgStatusLine.statusCode = STATUS_413;
		return false;
	}

	// check if Host field is present
	if ( _request.getHost() != _config.getHost() + ":" + _config.getPort() ) {
		_msgStatusLine.statusCode = STATUS_400;
		return false;
	}

	// check if content length is smaller 0
	if ( _request.getContentLength < static_cast<ssize_t>(0) ) {
		_msgStatusLine.statusCode = STATUS_400;
		return false;
	}

	return true;
}

/* _checkRedirection:
*	Checks if the request URI is a redirection.
*		- yes ⇒ response with Location and 30X status code
*	Returns true if the request URI is a redirection, false otherwise.
*/
bool	Response::_checkRedirection()
{
	/* std::string	redirection = _config.getRedirection( _paths.confLocKey );

	if ( redirection != "" ) {
		_msgStatusLine.statusCode = STATUS_301;
		_msgHeader["Location"] = redirection;
		return true;
	} */
	return false;
}

/* _handleGet:
*	Handles a GET request.
*		1) Check if CGI is needed
*			- yes ⇒ call CGI
*		2) [no] Create GET response
*			- read file from path and store in _msgBody and set _msgBodyLength
*			- fill out _msgStatusLine and _msgHeader
*/
void	Response::_handleGet()
{
	/* CGI-PART!!! */
	/* if ( _config.getLocations[_paths.confLocKey].getCgiPath() != "" ) {
		// call CGI
	}
	else { */
		// create GET response
		if ( !_readFile( _paths.responseUri ) )
			_msgStatusLine.statusCode = STATUS_500;
		else
			_msgStatusLine.statusCode = STATUS_200;
		std::map<std::string, std::string>::iterator it = _httpStatusCodeLookup.find(_msgStatusLine.statusCode);
		if ( it != _httpStatusCodeLookup.end() )
			_msgStatusLine.reasonPhrase = it->second;
		else
			_msgStatusLine.reasonPhrase = "";
		
		//_msgHeader["Content-Type"] = ;
		_msgHeader["Content-Length"] = _msgBodyLength.toString();
}

/* _readFile:
*	Reads the file from the given path and returns it as a std::string.
*	Also stores the number of bytes read in _msgBodyLength.
*	Returns false if the file could not be opened or if the file could 
*		not be read. Otherwise true.
*/
bool	Response::_readFile( std::string path )
{
	//std::cout << "Reading file: " << path << std::endl;
	std::ifstream	file(path, std::ios::in | std::ios::binary);

	if ( !file.is_open() )
		return false;

	// Seek to the end of the file to determine its size
	file.seekg(0, std::ios::end);
	std::streampos fileSize = file.tellg();
	file.seekg(0, std::ios::beg);

	if ( fileSize == -1 ) {
        file.close();
		return false;
	}

	// Create a string with the size of the file and read the entire file into it
	std::string	content;
	content.resize(static_cast<size_t>(fileSize));
	if (!file.read(&content[0], fileSize)) {
		file.close();
		return false;
	}

	_msgBodyLength = fileSize;
	_msgBody = content;

	file.close();
	return true;

	
	/* std::string		line;
	std::string		body;
	std::ifstream	ifs(path.c_str());

	if ( !ifs.is_open() )
		return false;

	while ( getline( ifs, line ) )
		body += line + "\n";
	ifs.close();

	return body; */
}


/* _readHttpStatusCodeDatabase:
*	Reads the http status code database and stores it in _httpStatusCodeLookup.
*/
void	Response::_readHttpStatusCodeDatabase()
{
	std::ifstream	file( HTTP_STATS_CODE_FILE );
	std::string		line;
	std::string		status;

	if ( !file.is_open() )
	{
		std::cerr << "Error: could not open http status code database (" << DATABASE << ")" << std::endl;
		return ;
	}
	while ( getline( file, line ) )
	{
		if ( line.find(',') == std::string::npos )
			continue ;
		_httpStatusCodeLookup[atoi(line.substr( 0, line.find(',') - 1 ))] = line.substr( line.find(',') + 1 );
	}
	file.close();
}


/**************************************************************/
/*                     GETTER METHODS                         */
/**************************************************************/

/* getResponseMsg:
*	Returns the response message as one std::string.
*/
std::string	Response::getResponseMsg() const
{
	std::string		responseMsg;

	// Status Line + Header
	responseMsg = getMsgHeader();
	
	// Body
	responseMsg.append(_msgBody, 0, _msgBodyLength);

	return responseMsg
}

/* getMsgHeader:
*	Returns the header of the response message (inlcuding the status line) 
*	as one std::string.
*/
std::string	Response::getMsgHeader() const
{
	std::string		header;

	// Status Line
	header = _msgStatusLine.protocolVersion + " ";
	header += std::to_string(_msgStatusLine.statusCode) + " ";
	header += _msgStatusLine.reasonPhrase + "\r\n";

	// Header
	for ( std::map<std::string, std::string>::iterator it = _msgHeader.begin(); it != _msgHeader.end(); it++ )
		header += it->first + ": " + it->second + "\r\n";
	
	header += "\r\n";

	return header;
}

/* getMsgLength:
*	Returns the length of the response message.
*/
ssize_t		Response::getMsgLength() const
{
	return getMsgHeader().length() + _msgBodyLength;
}





/**************************************************************/
/*                  PROTOTYPING METHODS                       */
/**************************************************************/


// /* buildResponse():
// *	Build the response corresponding to the _request and stores it in _response.
// *	1st draft contains:
// *		- filling out all values from _request
// *		- "Hello World" in the body
// *		- status line ("HTTP/1.1 200 OK"), "Content-Length" (text/html) and "Content-Length" in the header
// */
// void	Response::_buildResponse( std::string path )
// {
// 	std::string prefix1 = "/";
// 	std::string prefix2 = "/surfer.jpeg";
// 	std::string prefix3 = "/giga-chad-theme.mp3";

// 	//if (_request.buffer.find(prefix1) == 0)
// 	if ( path == prefix1 )
// 	{
// 		_msgBody = _readFile( "./html" + path + "index.html" );
// 		//_msgBody = "Hello World\n";
// 		std::stringstream	strStream;
// 		strStream << (_msgBody.length());

// 		_msgStatusLine = PROTOCOL_VERSION; 
// 		_msgStatusLine += " 200 OK\r\n";
// 		_msgHeader += "Content-Type: text/html\r\n";	
// 		_msgHeader += "Content-Length: " + strStream.str() + "\r\n";
// 		_msgHeader += "Connection: close\r\n";
// 		_msgHeader += "\r\n";
// 	}
// 	else if ( path == prefix2 )
// 	{
// 		_msgBody = _readFile( "./html" + path );
// 		std::stringstream	strStream;
// 		strStream << (_msgBody.length());

// 		_msgStatusLine = PROTOCOL_VERSION; 
// 		_msgStatusLine += " 200 OK\r\n";
// 		_msgHeader += "Content-Type: image/*\r\n";
// 		_msgHeader += "Content-Length: " + strStream.str() + "\r\n";
// 		_msgHeader += "Connection: close\r\n";
// 		_msgHeader += "\r\n";
// 	}
// 	else if ( path == prefix3 )
// 	{
// 		_msgBody = _readFile( "./html" + path );
// 		std::stringstream	strStream;
// 		strStream << (_msgBody.length());

// 		_msgStatusLine = PROTOCOL_VERSION; 
// 		_msgStatusLine += " 200 OK\r\n";
// 		_msgHeader += "Content-Type: audio/*\r\n";
// 		_msgHeader += "Content-Length: " + strStream.str() + "\r\n";
// 		_msgHeader += "Connection: close\r\n";
// 		_msgHeader += "\r\n";
// 	}
// 	_msgLength = _msgStatusLine.length() + _msgHeader.length() + _msgBody.length();
// }

// /* FOR CGI-TESTING */
// void	Response::_buildResponseCGI( std::string path )
// {
// 	path = "";
// 	std::string output;
// 	CGIHandler	CGIHandler;
// 	_msgBody = CGIHandler.execute();
// 	std::stringstream	strStream;
// 	strStream << (_msgBody.length());

// 	_msgStatusLine = "HTTP/1.1 200 OK\r\n";
// 	_msgHeader += "Content-Type: text/html\r\n";
// 	_msgHeader += "Content-Length: " + strStream.str() + "\r\n";
// 	_msgHeader += "\r\n";
// 	_msgLength = _msgStatusLine.length() + _msgHeader.length() + _msgBody.length();
// }


// void	ClientSocket::_saveFile( std::string path, std::string content )
// {
// 	std::cout << "Saving file: " << path << std::endl;
// 	std::ofstream	ofs(path.c_str());

// 	if ( !ofs.is_open() )
// 		throw std::runtime_error("Error: saveFile() failed");

// 	ofs << content;
// 	ofs.close();
// }



