/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <libdevcore/CommonIO.h>
#include <test/libsolidity/AnalysisFramework.h>
#include <test/libsolidity/SyntaxTest.h>

#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>

#include <cstdlib>
#include <iostream>
#include <fstream>

using namespace dev;
using namespace dev::solidity;
using namespace dev::solidity::test;
using namespace std;
namespace po = boost::program_options;
namespace fs = boost::filesystem;

class SyntaxTestTool: FormattedPrinter
{
public:
	SyntaxTestTool(string const& _name, fs::path const& _path, bool _colored):
		FormattedPrinter(_colored), m_name(_name), m_path(_path)
	{}

	bool process(int& _successCount);

	static bool processPath(
		fs::path const& _basepath,
		fs::path const& _path,
		int& _successCount,
		int& _runCount
	);

	static string editor;
	static bool noColor;
private:
	bool handleResponse(int& _successCount, bool const _parserError);

	void printContract() const;

	string const m_name;
	fs::path const m_path;
	unique_ptr<SyntaxTest> m_test;
};

bool SyntaxTestTool::noColor = false;
string SyntaxTestTool::editor;

void SyntaxTestTool::printContract() const
{
	stringstream stream(m_test->source());
	string line;
	Scope formattedStream = format(cout, {CYAN});
	while (getline(stream, line))
		formattedStream << "    " << line << endl;
	stream << endl;
}

bool SyntaxTestTool::process(int& _successCount)
{
	bool success;
	bool parserError = false;
	std::stringstream outputMessages;

	format(cout, {BOLD}) << m_name << ": ";
	cout.flush();

	try
	{
		m_test = unique_ptr<SyntaxTest>(new SyntaxTest(m_path.string(), !noColor));
	}
	catch (std::exception const& _e)
	{
		format(cout, {RED}) << "cannot read test: " << _e.what() << endl;
		return true;
	}

	try
	{
		success = m_test->run(outputMessages, "  ", 2);
	}
	catch (...)
	{
		success = false;
		parserError = true;
	}

	if (success)
	{
		format(cout, {GREEN}) << "OK";
		cout << endl;
		++_successCount;
		return true;
	}
	else
	{
		format(cout, {RED}) << "FAIL";
		cout << endl;

		cout << "  Contract:" << endl;
		printContract();

		if (parserError)
		{
			cout << "  ";
			format(cout, {INVERSE, RED}) << "Parsing failed:" << endl;
			m_test->printErrorList(cout, m_test->compilerErrors(), "    ", true, true);
			cout << endl;
		}
		else
			cout << outputMessages.str() << endl;

		return handleResponse(_successCount, parserError);
	}
}

bool SyntaxTestTool::handleResponse(int& _successCount, bool const _parserError)
{
	if (_parserError)
		cout << "(e)dit/(s)kip/(q)uit? ";
	else
		cout << "(e)dit/(u)pdate expectations/(s)kip/(q)uit? ";
	cout.flush();

	while (true)
	{
		switch(readStandardInputChar())
		{
		case 's':
			cout << endl;
			return true;
		case 'u':
			if (_parserError)
				break;
			else
			{
				cout << endl;
				ofstream file(m_path.string(), ios::trunc);
				file << m_test->source();
				file << "// ----" << endl;
				if (!m_test->errorList().empty())
				{
					m_test->disableFormatting();
					m_test->printErrorList(file, m_test->errorList(), "// ", false, false);
					if (!noColor)
						m_test->enableFormatting();
				}
				cout << "Re-running test case..." << endl;
				return process(_successCount);
			}
		case 'e':
			cout << endl << endl;
			if (system((editor + " " + m_path.string()).c_str()))
				cerr << "Error running editor command." << endl << endl;
			cout << "Re-running test case..." << endl;
			return process(_successCount);
		case 'q':
			cout << endl;
			return false;
		default:
			break;
		}
	}
}


bool SyntaxTestTool::processPath(
	fs::path const& _basepath,
	fs::path const& _path,
	int& _successCount,
	int& _runCount
)
{
	fs::path fullpath = _basepath / _path;
	if (fs::is_directory(fullpath))
	{
		for (auto const& entry: boost::iterator_range<fs::directory_iterator>(
			fs::directory_iterator(fullpath),
			fs::directory_iterator()
		))
			if (!processPath(_basepath, _path / entry.path().filename(), _successCount, _runCount))
				return false;
	}
	else
	{
		SyntaxTestTool testTool(_path.string(), fullpath, !noColor);
		++_runCount;
		if (!testTool.process(_successCount))
			return false;
	}
	return true;
}

int main(int argc, char *argv[])
{
	if (getenv("EDITOR"))
		SyntaxTestTool::editor = getenv("EDITOR");

	fs::path testPath;
	po::options_description options(
		R"(isoltest, tool for interactively managing test contracts.
Usage: isoltest [Options] --testpath path
Interactively validates test contracts.

Allowed options)",
		po::options_description::m_default_line_length,
		po::options_description::m_default_line_length - 23);
	options.add_options()
		("help", "Show this help screen.")
		("testpath", po::value<fs::path>(&testPath)->required(), "path to test files")
		(
			"no-color",
			po::bool_switch(&SyntaxTestTool::noColor)->default_value(false),
			"don't use colors"
		)
		("editor", po::value<string>(&SyntaxTestTool::editor), "editor for opening contracts");

	po::variables_map arguments;
	try
	{
		po::command_line_parser cmdLineParser(argc, argv);
		cmdLineParser.options(options);
		po::store(cmdLineParser.run(), arguments);

		if (arguments.count("help"))
		{
			cout << options << endl;
			return 0;
		}

		po::notify(arguments);
	}
	catch (po::error const& _exception)
	{
		cerr << _exception.what() << endl;
		return 1;
	}

	if (fs::exists(testPath) && fs::is_directory(testPath))
	{
		int runCount = 0;
		int successCount = 0;
		SyntaxTestTool::processPath(testPath / "libsolidity", "syntaxTests", successCount, runCount);

		cout << endl << "Summary: ";
		if (!SyntaxTestTool::noColor)
			cout << (runCount == successCount ? FormattedPrinter::GREEN : FormattedPrinter::RED);
		cout << successCount << "/" << runCount;
		if (!SyntaxTestTool::noColor)
			cout << FormattedPrinter::RESET;
		cout << " tests successful." << endl;

		return (runCount == successCount) ? 0 : 1;
	}
	else
	{
		cerr << "test path does not exist" << endl;
		return 1;
	}
}
