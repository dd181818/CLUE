#ifndef CLUE_FORMATTING_BASE__
#define CLUE_FORMATTING_BASE__

#include <clue/string_view.hpp>
#include <clue/internal/numfmt.hpp>
#include <clue/internal/grisu.hpp>
#include <string>
#include <cstdarg>
#include <stdexcept>

namespace clue {

namespace fmt {


//===============================================
//
//  Formatting flags
//
//===============================================

enum {
    upper_case = 0x01,
    pad_zeros = 0x02,
    plus_sign = 0x04,
    left_just = 0x08,
    quoted = 0x10
};

typedef unsigned int flag_t;

//===============================================
//
//  C-format
//
//===============================================

inline ::std::string c_sprintf(const char *fmt, ...) {
    std::va_list args0, args;
    va_start(args0, fmt);
    va_copy(args, args0);
    size_t n = (size_t)::std::vsnprintf(nullptr, 0, fmt, args0);
    va_end(args0);
    ::std::string str(n, '\0');
    if (n > 0) {
        ::std::vsnprintf(const_cast<char*>(str.data()), n+1, fmt, args);
    }
    va_end(args);
    return ::std::move(str);
}


//===============================================
//
//  Integer formatting
//
//===============================================

// ndigits

template<typename T>
inline size_t ndigits(T x, const unsigned base) noexcept {
    auto u = details::uabs(x);
    switch (base) {
        case  8: return details::ndigits_oct(u);
        case 10: return details::ndigits_dec(u);
        case 16: return details::ndigits_hex(u);
    }
    return 0;
}

namespace details {

} // end namespace details


// length of formatted integer

class int_formatter {
private:
    unsigned base_;
    size_t width_;
    flag_t flags_;

public:
    // construction & properties

    constexpr int_formatter() noexcept :
        base_(10), width_(0), flags_(0) {}

    explicit constexpr int_formatter(unsigned base) noexcept :
        base_(base), width_(0), flags_(0) {}

    constexpr int_formatter(unsigned base, size_t width, flag_t flags) :
        base_(base), width_(width), flags_(flags) {}

    constexpr unsigned base() const noexcept { return base_; }
    constexpr size_t width() const noexcept { return width_; }
    constexpr flag_t flags() const noexcept { return flags_; }

    constexpr int_formatter base(unsigned v) const noexcept {
        return int_formatter(v, width_, flags_);
    }

    constexpr int_formatter width(size_t v) const noexcept {
        return int_formatter(base_, v, flags_);
    }

    constexpr int_formatter flags(flag_t v) const noexcept {
        return int_formatter(base_, width_, v);
    }

    constexpr int_formatter operator | (flag_t v) const noexcept {
        return int_formatter(base_, width_, flags_ | v);
    }

    constexpr bool any(flag_t msk) const noexcept {
        return static_cast<bool>(flags_ & msk);
    }

    // formatting

    template<typename T>
    size_t max_formatted_length(T x) const noexcept {
        size_t n = ndigits(x, base_);
        if (x < 0 || any(plus_sign)) n++;
        return n > width_ ? n : width_;
    }

    template<typename T, typename charT>
    size_t formatted_write(T x, charT *buf, size_t buf_len) const {
        auto ax = details::uabs(x);
        size_t nd = ndigits(ax, base_);
        char sign = x < 0 ? '-' : (any(plus_sign) ? '+' : '\0');
        size_t flen = nd + (sign ? 1 : 0);
        assert(buf_len > flen);

        charT *p = buf;
        if (width_ > flen) {
            size_t plen = width_ - flen;
            if (any(pad_zeros)) {
                // pad zeros
                if (sign) *(p++) = sign;
                for (size_t i = 0; i < plen; ++i) *(p++) = (charT)('0');
            } else {
                // pad empty
                for (size_t i = 0; i < plen; ++i) *(p++) = (charT)(' ');
                if (sign) *(p++) = sign;
            }
        } else {
            // no padding
            if (sign) *(p++) = sign;
        }
        details::extract_digits(ax, base_, any(upper_case), p, nd);
        p[nd] = '\0';
        return p + nd - buf;
    }
};

constexpr int_formatter oct_fmt() noexcept { return int_formatter(8);  }
constexpr int_formatter dec_fmt() noexcept { return int_formatter(10); }
constexpr int_formatter hex_fmt() noexcept { return int_formatter(16); }

// simplify version which takes advantage of the default setting
class default_int_formatter {
public:
    // properties

    constexpr unsigned base() const noexcept { return 10; }
    constexpr size_t width() const noexcept { return 0; }
    constexpr flag_t flags() const noexcept { return 0; }

    constexpr int_formatter base(unsigned v) const noexcept {
        return int_formatter(v, 0, 0);
    }

    constexpr int_formatter width(size_t v) const noexcept {
        return int_formatter(10, v, 0);
    }

    constexpr int_formatter flags(flag_t v) const noexcept {
        return int_formatter(10, 0, v);
    }

    constexpr int_formatter operator | (flag_t v) const noexcept {
        return int_formatter(10, 0, v);
    }

    constexpr bool any(flag_t msk) const noexcept {
        return false;
    }

    // formatting

    template<typename T>
    size_t max_formatted_length(T x) const noexcept {
        size_t n = details::ndigits_dec(details::uabs(x));
        if (x < 0) n++;
        return n;
    }

    template<typename T, typename charT>
    size_t formatted_write(T x, charT *buf, size_t buf_len) const {
        auto ax = details::uabs(x);
        size_t nd = details::ndigits_dec(ax);

        charT *p = buf;
        size_t flen = nd;
        if (x < 0) {
            flen++;
            CLUE_ASSERT(buf_len > flen);
            *(p++) = '-';
        } else {
            CLUE_ASSERT(buf_len > flen);
        }
        details::extract_digits_dec(ax, p, nd);
        p[nd] = '\0';
        return flen;
    }
};

constexpr default_int_formatter default_int_fmt() noexcept {
    return default_int_formatter{};
}

//===============================================
//
//  Floating-point formatting
//
//===============================================

namespace details {

struct fixed_t {};
struct sci_t {};

template<typename Tag>
struct float_fmt_traits {};

template<>
struct float_fmt_traits<fixed_t> {
    static size_t maxfmtlength(double x, size_t precision, bool plus_sign) noexcept {
        return maxfmtlength_fixed(x, precision, plus_sign);
    }

    static constexpr char printf_sym(bool upper) noexcept {
        return upper ? 'F' : 'f';
    }
};

template<>
struct float_fmt_traits<sci_t> {
    static size_t maxfmtlength(double x, size_t precision, bool plus_sign) noexcept {
        return maxfmtlength_sci(x, precision, plus_sign);
    }

    static constexpr char printf_sym(bool upper) noexcept {
        return upper ? 'E' : 'e';
    }
};

} // end namespace details


template<typename Tag>
class float_formatter {
private:
    typedef details::float_fmt_traits<Tag> fmt_traits_t;
    size_t width_;
    size_t precision_;
    flag_t flags_;

public:
    typedef Tag tag_type;

    // construction & properties

    constexpr float_formatter() noexcept :
        width_(0), precision_(6), flags_(0) {}

    constexpr float_formatter(size_t width, size_t precision, flag_t flags) :
        width_(width), precision_(precision), flags_(flags) {}

    constexpr size_t width() const noexcept { return width_; }
    constexpr size_t precision() const noexcept { return precision_; }
    constexpr flag_t flags() const noexcept { return flags_; }

    constexpr float_formatter width(size_t v) const noexcept {
        return float_formatter(v, precision_, flags_);
    }

    constexpr float_formatter precision(size_t v) const noexcept {
        return float_formatter(width_, v, flags_);
    }

    constexpr float_formatter flags(flag_t v) const noexcept {
        return float_formatter(width_, precision_, v);
    }

    constexpr float_formatter operator | (flag_t v) const noexcept {
        return float_formatter(width_, precision_, flags_ | v);
    }

    constexpr bool any(flag_t msk) const noexcept {
        return static_cast<bool>(flags_ & msk);
    }

    // formatting

    size_t max_formatted_length(double x) const noexcept {
        size_t n = 0;
        if (::std::isfinite(x)) {
            n = fmt_traits_t::maxfmtlength(x, precision_, any(plus_sign));
        } else if (::std::isinf(x)) {
            n = ::std::signbit(x) || any(plus_sign) ? 4 : 3;
        } else {
            CLUE_ASSERT(::std::isnan(x));
            n = any(plus_sign) ? 4 : 3;
        }
        return n > width_ ? n : width_;
    }

    template<typename charT>
    size_t formatted_write(double x, charT *buf, size_t buf_len) const {
        char cfmt[16];
        const char fsym =
                details::float_fmt_traits<Tag>::printf_sym(any(upper_case));
        details::float_cfmt_impl(cfmt, fsym, width_, precision_,
                any(plus_sign), any(pad_zeros));
        size_t n = (size_t)::std::snprintf(buf, buf_len, cfmt, x);
        CLUE_ASSERT(n < buf_len);
        return n;
    }
};


class grisu_formatter {
public:
    size_t max_formatted_length(double x) const noexcept {
        return 27;
    }

    template<typename charT>
    size_t formatted_write(double x, charT *buf, size_t buf_len) const {
        size_t n = (size_t)grisu_impl::dtoa(x, buf);
        CLUE_ASSERT(n < buf_len);
        return n;
    }
};

using default_float_formatter = grisu_formatter;
using fixed_formatter = float_formatter<details::fixed_t>;
using sci_formatter = float_formatter<details::sci_t>;

constexpr fixed_formatter fixed_fmt() noexcept {
    return fixed_formatter();
}

constexpr sci_formatter sci_fmt() noexcept {
    return sci_formatter();
}

constexpr grisu_formatter default_float_fmt() noexcept {
    return default_float_formatter{};
}


//===============================================
//
//  Char & String formatting
//
//===============================================

class default_char_formatter {
public:
    template<typename charT>
    constexpr size_t max_formatted_length(charT c) const noexcept {
        return 1;
    }

    template<typename charT>
    size_t formatted_write(charT c, charT *buf, size_t buf_len) const {
        buf[0] = c;
        buf[1] = '\0';
        return 1;
    }
};

class default_string_formatter {
public:
    template<typename charT>
    constexpr size_t max_formatted_length(const charT *sz) const noexcept {
        return ::std::char_traits<charT>::length(sz);
    }

    template<typename charT, typename Traits, typename Allocator>
    constexpr size_t max_formatted_length(
            const ::std::basic_string<charT, Traits, Allocator>& s) const noexcept {
        return s.size();
    }

    template<typename charT, typename Traits>
    constexpr size_t max_formatted_length(
            const basic_string_view<charT, Traits>& sv) const noexcept {
        return sv.size();
    }

    template<typename charT>
    size_t formatted_write(
            const charT* s,
            charT *buf, size_t buf_len) const noexcept {

        const charT *p = s;
        const charT *pend = s + buf_len;
        while (*p && p != pend) *buf++ = *p++;
        *buf = '\0';
        return static_cast<size_t>(p - s);
    }

    template<typename charT, typename Traits, typename Allocator>
    size_t formatted_write(
            const ::std::basic_string<charT, Traits, Allocator>& s,
            charT *buf, size_t buf_len) const noexcept {
        return formatted_write_(s.data(), s.size(), buf, buf_len);

    }

    template<typename charT, typename Traits>
    size_t formatted_write(
            const basic_string_view<charT, Traits>& sv,
            charT *buf, size_t buf_len) const noexcept {
        return formatted_write_(sv.data(), sv.size(), buf, buf_len);
    }

private:
    template<typename charT>
    size_t formatted_write_(
            const charT *src, size_t n, charT *buf, size_t buf_len) const noexcept {
        CLUE_ASSERT(n < buf_len);
        ::std::memcpy(buf, src, n * sizeof(charT));
        return n;
    }
};


//===============================================
//
//  Generic formatting
//
//===============================================

// Generic formatting setting

// for arithmetic types

template<typename T, typename charT>
struct is_default_formattable : public ::std::is_arithmetic<T> {};

template<typename T>
constexpr enable_if_t<::std::is_integral<T>::value, default_int_formatter>
default_formatter(const T& x) noexcept {
    return default_int_fmt();
};

template<typename T>
constexpr enable_if_t<::std::is_floating_point<T>::value, grisu_formatter>
default_formatter(const T& x) noexcept {
    return default_float_fmt();
};

// for characters

template<typename charT>
struct is_default_formattable<charT, charT> : public ::std::true_type {};

constexpr default_char_formatter default_formatter(char) noexcept {
    return default_char_formatter{};
}

constexpr default_char_formatter default_formatter(wchar_t) noexcept {
    return default_char_formatter{};
}

constexpr default_char_formatter default_formatter(char16_t) noexcept {
    return default_char_formatter{};
}

constexpr default_char_formatter default_formatter(char32_t) noexcept {
    return default_char_formatter{};
}

// for string related types

template<typename charT>
struct is_default_formattable<charT*, charT> : public ::std::true_type {};

template<typename charT>
struct is_default_formattable<const charT*, charT> : public ::std::true_type {};

template<typename charT, typename Traits>
struct is_default_formattable<
    basic_string_view<charT, Traits>, charT> : public ::std::true_type {};

template<typename charT, typename Traits, typename Allocator>
struct is_default_formattable<
    ::std::basic_string<charT, Traits, Allocator>, charT> : public ::std::true_type {};

constexpr default_string_formatter default_formatter(const char*) noexcept {
    return default_string_formatter{};
}

constexpr default_string_formatter default_formatter(const wchar_t*) noexcept {
    return default_string_formatter{};
}

constexpr default_string_formatter default_formatter(const char16_t*) noexcept {
    return default_string_formatter{};
}

constexpr default_string_formatter default_formatter(const char32_t*) noexcept {
    return default_string_formatter{};
}

template<typename charT, typename Traits, typename Allocator>
constexpr default_string_formatter default_formatter(
        const ::std::basic_string<charT, Traits, Allocator>&) noexcept {
    return default_string_formatter{};
}

template<typename charT, typename Traits>
constexpr default_string_formatter default_formatter(
        const basic_string_view<charT, Traits>&) noexcept {
    return default_string_formatter{};
}


} // end namespace fmt
} // end namespace clue

#endif