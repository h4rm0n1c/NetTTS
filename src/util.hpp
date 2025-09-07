#pragma once
#include <string>
#include <windows.h>

void rtrim(std::string& s);
std::wstring u8_to_w(const std::string& s);
std::string  w_to_u8(const std::wstring& w);
bool is_digits(const std::wstring& s);
bool is_digits_token(const std::string& s);
