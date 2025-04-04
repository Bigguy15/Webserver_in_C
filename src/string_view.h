#ifndef __STRING_VIEW_H__
#define __STRING_VIEW_H__
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

typedef struct {
    const char* data;
    size_t size;
} string_view;

#define SV_FMT "%.*s"
#define SV_ARG(sv) (int)(sv).size, (sv).data
#define SV_ARG_POINTER(sv) (int)(sv)->size, (sv)->data
#define SV_EMPTY sv_create("", 0)
#define SV_LITERAL(__literal__) (string_view) { .data = (__literal__), .size = sizeof(__literal__) - 1 }


/**
 * @brief Creates a string_view from given data and size.
 * 
 * @param data Pointer to the character data.
 * @param size Size of the character data.
 * @return string_view The created string_view.
 */
string_view sv_create(const char* data, size_t size);

/**
 * @brief Chops the first n characters from the string_view.
 * 
 * @param sv Pointer to the string_view to chop from.
 * @param n Number of characters to chop.
 * @return string_view The chopped string_view.
 */
string_view sv_chop(string_view *sv, size_t n);

string_view sv_chop_right(string_view *sv, size_t n);
/**
 * @brief Chops the prefix from the string_view if it matches the given prefix.
 * 
 * @param sv Pointer to the string_view to chop from.
 * @param other The prefix to match and chop.
 * @return string_view The chopped prefix string_view.
 */
string_view sv_chop_prefix(string_view *sv, string_view other);
string_view sv_chop_prefix_right(string_view *sv, string_view other);
/**
 * @brief Chops the prefix from the string_view if it matches the given char.
 * 
 * @param sv Pointer to the string_view to chop from.
 * @param limiter The char to match and chop.
 * @return string_view The chopped char string_view.
 */

string_view sv_chop_char(string_view *sv, char limiter);
/**
 * @brief Compares two string_views for equality.
 * 
 * @param sv The first string_view.
 * @param other The second string_view.
 * @param ignorecase Whether to ignore case in comparison.
 * @return true If the string_views are equal.
 * @return false If the string_views are not equal.
 */
bool sv_equals(string_view sv, string_view other, bool ignorecase);

/**
 * @brief Checks if the string_view has the given prefix.
 * 
 * @param sv The string_view to check.
 * @param other The prefix to check for.
 * @param ignorecase Whether to ignore case in comparison.
 * @return true If the string_view has the prefix.
 * @return false If the string_view does not have the prefix.
 */
bool sv_has_prefix(string_view sv, string_view other, bool ignorecase);

#endif // __STRING_VIEW_H__

#ifdef SV_IMPLEMENTATION

string_view sv_create(const char* data, size_t size) {
    string_view sv;
    sv.data = data;
    sv.size = size;
    return sv;
}
string_view sv_chop_right(string_view *sv, size_t n){
    if (sv->size < n) {
        sv->size = n;
    }
    string_view result = sv_create(sv->data + sv->size - n,n);
    sv->size -= n;
    return result;
}
string_view sv_chop(string_view *sv, size_t n) {
    if (sv->size < n) {
        sv->size = n;
    }
    string_view result = sv_create(sv->data, n);
    sv->size -= n;
    sv->data += n;
    return result;
}
string_view sv_chop_prefix(string_view *sv, string_view other) {
    if (!sv_has_prefix(*sv, other, true)) {
        return sv_create(NULL, 0);
    }
    return sv_chop(sv, other.size);
}
string_view sv_chop_char(string_view *sv, char limiter){
    size_t i = 0;
    while(i<sv->size && sv->data[i] != limiter){
        i+=1;
    }
    string_view result= sv_create(sv->data,i);
    if (i < sv->size) {
        sv->size -= i + 1;
        sv->data  += i + 1;
    } else {
        sv->size -= i;
        sv->data  += i;
    }
    return result;
}

bool sv_equals(string_view sv, string_view other, bool ignorecase) {
    if (sv.size != other.size) {
        return false;
    }
    if (!ignorecase) {
        return memcmp(sv.data, other.data, sv.size) == 0;
    } else {
        char x, y;
        for (size_t i = 0; i < sv.size; i++) {
            x = 'A' <= sv.data[i] && sv.data[i] <= 'Z'
                  ? sv.data[i] + 32
                  : sv.data[i];
    
            y = 'A' <= other.data[i] && other.data[i] <= 'Z'
                  ? other.data[i] + 32
                  : other.data[i];
    
            if (x != y) return false;
        }
        return true; 
    }
}

bool sv_has_prefix(string_view sv, string_view other, bool ignorecase) {
    if (sv.size < other.size) {
        return false;
    }
    string_view tempstr = sv_create(sv.data, other.size);
    return sv_equals(tempstr, other, ignorecase);
}

#endif // SV_IMPLEMENTATION