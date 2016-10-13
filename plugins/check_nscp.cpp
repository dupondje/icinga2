/******************************************************************************
* Icinga 2                                                                   *
* Copyright (C) 2012-2016 Icinga Development Team (https://www.icinga.org/)  *
*                                                                            *
* This program is free software; you can redistribute it and/or              *
* modify it under the terms of the GNU General Public License                *
* as published by the Free Software Foundation; either version 2             *
* of the License, or (at your option) any later version.                     *
*                                                                            *
* This program is distributed in the hope that it will be useful,            *
* but WITHOUT ANY WARRANTY; without even the implied warranty of             *
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the              *
* GNU General Public License for more details.                               *
*                                                                            *
* You should have received a copy of the GNU General Public License          *
* along with this program; if not, write to the Free Software Foundation     *
* Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.             *
******************************************************************************/

#define VERSION 0.1

#include "remote/httpclientconnection.hpp"
#include "remote/httprequest.hpp"
#include "remote/url-characters.hpp"
#include "base/application.hpp"
#include "base/json.hpp"
#include "base/string.hpp"
#include <boost/program_options.hpp>
#include <boost/algorithm/string/split.hpp>

using namespace icinga;
namespace po = boost::program_options;

static void ResultHttpCompletionCallback(const HttpRequest& request, HttpResponse& response, bool& ready,
    boost::condition_variable& cv, boost::mutex& mtx, Dictionary::Ptr& result)
{
	String body;
	char buffer[1024];
	size_t count;

	while ((count = response.ReadBody(buffer, sizeof(buffer))) > 0)
		body += String(buffer, buffer + count);

	result = JsonDecode(body);
	boost::mutex::scoped_lock lock(mtx);
	ready = true;
	cv.notify_all();
}

static Dictionary::Ptr QueryEndpoint(const String& host, const String& port, const String& password,
    const String& endpoint)
{
	HttpClientConnection::Ptr m_Connection = new HttpClientConnection(host, port, true);

	try {
		bool ready = false;
		boost::condition_variable cv;
		boost::mutex mtx;
		Dictionary::Ptr result;
		boost::shared_ptr<HttpRequest> req = m_Connection->NewRequest();
		req->RequestMethod = "GET";

		// Url() will call Utillity::UnescapeString() which will thrown an exception if it finds a lonely %
		req->RequestUrl = new Url(endpoint);
		req->AddHeader("password", password);
		m_Connection->SubmitRequest(req, boost::bind(ResultHttpCompletionCallback, _1, _2,
			boost::ref(ready), boost::ref(cv), boost::ref(mtx), boost::ref(result)));

		boost::mutex::scoped_lock lock(mtx);
		while (!ready) {
			cv.wait(lock);
		}

		return result;
	}
	catch (const std::exception& ex) {
		std::cout << "Caught an exception: " << ex.what() << '\n';
		return Dictionary::Ptr();
	}
}

static int FormatOutput(const Dictionary::Ptr& result)
{
	if (!result) {
		std::cout << "check_nscp UNKNOWN No data received.\n";
		return 3;
	}

	Array::Ptr payloads = result->Get("payload");
	if (!payloads) {
		std::cout << "check_nscp UNKNOWN Answer format error: Answer is missing 'payload'.\n";
		return 3;
	}

	if (payloads->GetLength() == 0) {
		std::cout << "check_nscp UNKNOWN Answer format error: 'payload' was empty.\n";
		return 3;
	}

	Dictionary::Ptr payload;
	try {
		payload = payloads->Get(0);
	} catch (const std::exception& ex) {
		std::cout << "check_nscp UNKNOWN Answer format error: 'payload' was not a Dictionary.\n";
		return 3;
	}

	
	Array::Ptr lines;
	try {
		lines = payload->Get("lines");
	} catch (const std::exception&) {
		std::cout << "check_nscp UNKNOWN Answer format error: 'payload' is missing 'lines'.\n";
		return 3;
	}

	if (!lines) {
		std::cout << "check_nscp UNKNOWN Answer format error: 'lines' is Null.\n";
		return 3;
	}

	std::stringstream ssout;
	ObjectLock olock(lines);

	for (const Value& vline : lines) {
		Dictionary::Ptr line;
		try {
			line = vline;
		} catch (const std::exception& ex) {
			std::cout << "check_nscp UNKNOWN Answer format error: 'lines' entry was not a Dictionary.\n";
			return 3;
		}
		if (!line) {
			std::cout << "check_nscp UNKNOWN Answer format error: 'lines' entry was Null.\n";
			return 3;
		}

		ssout << payload->Get("command") << ' ' << line->Get("message") << " | ";

		if (!line->Contains("perf")) {
			ssout << '\n';
			break;
		}

		Array::Ptr perfs = line->Get("perf");
		ObjectLock olock(perfs);

		for (const Dictionary::Ptr& perf : perfs) {
			ssout << "'" << perf->Get("alias") << "'=";
			Dictionary::Ptr values = perf->Contains("int_value") ? perf->Get("int_value") : perf->Get("float_value");
			//TODO: Test ohne unit
			ssout << values->Get("value") << values->Get("unit") << ';' << values->Get("warning") << ';' << values->Get("critical");

			if (values->Contains("minimum") || values->Contains("maximum")) {
				ssout << ';';

				if (values->Contains("minimum"))
					ssout << values->Get("minimum");

				if (values->Contains("maximum"))
					ssout << ';' << values->Get("maximum");
			}

			ssout << ' ';
		}

		ssout << '\n';
	}

	String state = static_cast<String>(payload->Get("result")).ToUpper();
	int creturn = 0 ? state == "OK" : 1 ? state == "WARNING" : 2 ? state == "CRITICAL" : 3;

	if (creturn == 3)
		std::cout << "check_nscp UNKNOWN Answer format error: 'result' was not a known state.\n";
	else
		std::cout << ssout.rdbuf();
	return creturn;
}

void main(int argc, char **argv)
{
	po::variables_map vm;
	po::options_description desc("Options");

	desc.add_options()
		("help,h", "Print usage message and exit")
		("version,V", "Print version and exit")
		("debug,d", "Verbose/Debug output")
		("host,H", po::value<String>()->required(), "REQUIRED: NSCP-Host")
		("port,P", po::value<String>()->default_value("8443"), "NSCP-Port (Default: 8443)")
		("passwd", po::value<String>()->required(), "REQUIRED: NSCP-Password")
		("query,q", po::value<String>()->required(), "REQUIRED: Endpoint to query")
		("arg,a", po::value<std::vector<String>>()->multitoken(), "Arguments which to sent to the endpoint");

	po::basic_command_line_parser<char> parser(argc, argv);

	try {
		po::store(
			parser
			.options(desc)
			.style(
				po::command_line_style::unix_style |
				po::command_line_style::allow_long_disguise)
			.run(),
			vm);

		if (vm.count("version")) {
			std::cout << "Version: " << VERSION << '\n';
			Application::Exit(0);
		}

		if (vm.count("help")) {
			//TODO: Actual help text
			std::cout << argv[0] << " Help\n\tVersion: " << VERSION << '\n';
			std::cout << desc;
			Application::Exit(0);
		}

		vm.notify();
	} catch (std::exception& e) {
		std::cout << e.what() << '\n' << desc << '\n';
		Application::Exit(3);
	}

	String endpoint = "/query/" + vm["query"].as<String>();
	if (!vm.count("arg"))
		endpoint += '/';
	else {
		endpoint += '?';
		for (String arg : vm["arg"].as<std::vector<String>>()) {
			String::SizeType pos = arg.FindFirstOf("=");
			if (pos == String::NPos)
				endpoint += Utility::EscapeString(arg, ACQUERY_ENCODE, false);
			else {
				String key = arg.SubStr(0, pos);
				String val = arg.SubStr(pos + 1);
				endpoint += Utility::EscapeString(key, ACQUERY_ENCODE, false) + "=" + Utility::EscapeString(val, ACQUERY_ENCODE, false);
			}
			endpoint += '&';
		}
	}

	Application::InitializeBase();

	Dictionary::Ptr result = QueryEndpoint(vm["host"].as<String>(), vm["port"].as<String>(),
	    vm["passwd"].as<String>(), endpoint);

	Application::Exit(FormatOutput(result));
}