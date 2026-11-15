// Reading values the user types into a host's parameter field. Free of VST3
// types, so the tests can check it. Taken unchanged from Torus Classic 0.2.0
// (there from Toy Piano Classic 0.3.2), apart from the namespace.
#pragma once

#include <cctype>
#include <charconv>
#include <cmath>
#include <string>
#include <string_view>
#include <system_error>

namespace elements_synth {

namespace detail {

inline std::string_view Trim(std::string_view text) {
    while(!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while(!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    return text;
}

inline bool EndsWithIgnoringCase(std::string_view text, std::string_view suffix) {
    if(suffix.empty() || text.size() < suffix.size())
        return false;
    const std::string_view end = text.substr(text.size() - suffix.size());
    for(std::size_t i = 0; i < suffix.size(); ++i)
        if(std::tolower(static_cast<unsigned char>(end[i]))
           != std::tolower(static_cast<unsigned char>(suffix[i])))
            return false;
    return true;
}

} // namespace detail

// The text without surrounding blanks and without the unit, if it ends in it.
inline std::string_view StripUnit(std::string_view text, std::string_view unit) {
    text = detail::Trim(text);
    if(detail::EndsWithIgnoringCase(text, unit))
        text = detail::Trim(text.substr(0, text.size() - unit.size()));
    return text;
}

inline bool EqualsIgnoringCase(std::string_view a, std::string_view b) {
    return a.size() == b.size() && (a.empty() || detail::EndsWithIgnoringCase(a, b));
}

// One finite number and nothing else, apart from blanks and the unit: "500",
// " 50 % ", "+2.5", "2,5". Rejects empty text, other words, trailing rest
// ("12junk"), a second sign ("+-5"), and infinity or NaN. Independent of the
// C locale. `value` is only written on success.
inline bool ParseTypedNumber(std::string_view text, std::string_view unit, double& value) {
    std::string number(StripUnit(text, unit));
    if(!number.empty() && number.front() == '+') {
        number.erase(0, 1);
        if(number.empty() || number.front() == '+' || number.front() == '-')
            return false;
    }
    if(number.find('.') == std::string::npos) {
        const std::size_t comma = number.find(',');
        if(comma != std::string::npos && number.find(',', comma + 1) == std::string::npos)
            number[comma] = '.'; // a decimal comma, as typed on a German keyboard
    }
    if(number.empty())
        return false;
    double parsed = 0.0;
    const char* begin = number.data();
    const char* end = begin + number.size();
    const auto [stop, error] = std::from_chars(begin, end, parsed);
    if(error != std::errc() || stop != end || !std::isfinite(parsed))
        return false;
    value = parsed;
    return true;
}

} // namespace elements_synth
