#pragma once

#include <iomanip>
#include <sstream>
#include <string>

template<typename T>
std::string to_string(const T& t)
{
	std::stringstream ss;
	ss << t;
	return ss.str();
}

template<typename T>
std::string to_string(const T& t, const std::streamsize precision)
{
	std::stringstream ss;
	ss << std::setprecision(precision);
	ss << t;
	return ss.str();
}

template<typename T>
T from_string(const std::string& s)
{
	T value = T();
	std::stringstream ss(s);
	ss >> value;
	return value;
}
