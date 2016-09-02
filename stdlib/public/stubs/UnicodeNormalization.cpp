//===--- UnicodeNormalization.cpp - Unicode Normalization Helpers ---------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2016 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// Functions that use ICU to do unicode normalization and collation.
//
//===----------------------------------------------------------------------===//

#include "swift/Basic/Lazy.h"
#include "swift/Runtime/Config.h"
#include "swift/Runtime/Debug.h"

#include <algorithm>
#include <mutex>
#include <assert.h>

#include <unicode/ustring.h>
#include <unicode/ucol.h>
#include <unicode/ucoleitr.h>
#include <unicode/uiter.h>

#include "../SwiftShims/UnicodeShims.h"

static const UCollator *MakeRootCollator() {
  UErrorCode ErrorCode = U_ZERO_ERROR;
  UCollator *root = ucol_open("", &ErrorCode);
  if (U_FAILURE(ErrorCode)) {
    swift::crash("ucol_open: Failure setting up default collation.");
  }
  ucol_setAttribute(root, UCOL_NORMALIZATION_MODE, UCOL_ON, &ErrorCode);
  ucol_setAttribute(root, UCOL_STRENGTH, UCOL_TERTIARY, &ErrorCode);
  ucol_setAttribute(root, UCOL_NUMERIC_COLLATION, UCOL_OFF, &ErrorCode);
  ucol_setAttribute(root, UCOL_CASE_LEVEL, UCOL_OFF, &ErrorCode);
  if (U_FAILURE(ErrorCode)) {
    swift::crash("ucol_setAttribute: Failure setting up default collation.");
  }
  return root;
}

// According to this thread in the ICU mailing list, it should be safe
// to assume the UCollator object is thread safe so long as you're only
// passing it to functions that take a const pointer to it. So, we make it
// const here to make sure we don't misuse it.
// http://sourceforge.net/p/icu/mailman/message/27427062/
static const UCollator *GetRootCollator() {
  return SWIFT_LAZY_CONSTANT(MakeRootCollator());
}

/// This class caches the collation element results for the ASCII subset of
/// unicode.
class ASCIICollation {
  int32_t CollationTable[128];
public:
  friend class swift::Lazy<ASCIICollation>;

  static swift::Lazy<ASCIICollation> theTable;
  static const ASCIICollation *getTable() {
    return &theTable.get();
  }

  /// Maps an ASCII character to a collation element priority as would be
  /// returned by a call to ucol_next().
  int32_t map(unsigned char c) const {
    return CollationTable[c];
  }

private:
  /// Construct the ASCII collation table.
  ASCIICollation() {
    const UCollator *Collator = GetRootCollator();
    for (unsigned char c = 0; c < 128; ++c) {
      UErrorCode ErrorCode = U_ZERO_ERROR;
      intptr_t NumCollationElts = 0;
#if defined(__CYGWIN__) || defined(_MSC_VER)
      UChar Buffer[1];
#else
      uint16_t Buffer[1];
#endif
      Buffer[0] = c;

      UCollationElements *CollationIterator =
          ucol_openElements(Collator, Buffer, 1, &ErrorCode);

      while (U_SUCCESS(ErrorCode)) {
        intptr_t Elem = ucol_next(CollationIterator, &ErrorCode);
        if (Elem != UCOL_NULLORDER) {
          CollationTable[c] = Elem;
          ++NumCollationElts;
        } else {
          break;
        }
      }

      ucol_closeElements(CollationIterator);
      if (U_FAILURE(ErrorCode) || NumCollationElts != 1) {
        swift::crash("Error setting up the ASCII collation table");
      }
    }
  }

  ASCIICollation &operator=(const ASCIICollation &) = delete;
  ASCIICollation(const ASCIICollation &) = delete;
};

/// Compares the strings via the Unicode Collation Algorithm on the root locale.
/// Results are the usual string comparison results:
///  <0 the left string is less than the right string.
/// ==0 the strings are equal according to their collation.
///  >0 the left string is greater than the right string.
int32_t
swift::_swift_stdlib_unicode_compare_utf16_utf16(const uint16_t *LeftString,
                                                 int32_t LeftLength,
                                                 const uint16_t *RightString,
                                                 int32_t RightLength) {
#if defined(__CYGWIN__) || defined(_MSC_VER)
  // ICU UChar type is platform dependent. In Cygwin, it is defined
  // as wchar_t which size is 2. It seems that the underlying binary
  // representation is same with swift utf16 representation.
  return ucol_strcoll(GetRootCollator(),
    reinterpret_cast<const UChar *>(LeftString), LeftLength,
    reinterpret_cast<const UChar *>(RightString), RightLength);
#else
  return ucol_strcoll(GetRootCollator(),
    LeftString, LeftLength,
    RightString, RightLength);
#endif
}

/// Compares the strings via the Unicode Collation Algorithm on the root locale.
/// Results are the usual string comparison results:
///  <0 the left string is less than the right string.
/// ==0 the strings are equal according to their collation.
///  >0 the left string is greater than the right string.
int32_t
swift::_swift_stdlib_unicode_compare_utf8_utf16(const unsigned char *LeftString,
                                                int32_t LeftLength,
                                                const uint16_t *RightString,
                                                int32_t RightLength) {
  UCharIterator LeftIterator;
  UCharIterator RightIterator;
  UErrorCode ErrorCode = U_ZERO_ERROR;

  uiter_setUTF8(&LeftIterator, reinterpret_cast<const char *>(LeftString), LeftLength);
#if defined(__CYGWIN__) || defined(_MSC_VER)
  uiter_setString(&RightIterator, reinterpret_cast<const UChar *>(RightString),
                  RightLength);
#else
  uiter_setString(&RightIterator, RightString, RightLength);
#endif

  uint32_t Diff = ucol_strcollIter(GetRootCollator(),
    &LeftIterator, &RightIterator, &ErrorCode);
  if (U_FAILURE(ErrorCode)) {
    swift::crash("ucol_strcollIter: Unexpected error doing utf8<->utf16 string comparison.");
  }
  return Diff;
}

/// Compares the strings via the Unicode Collation Algorithm on the root locale.
/// Results are the usual string comparison results:
///  <0 the left string is less than the right string.
/// ==0 the strings are equal according to their collation.
///  >0 the left string is greater than the right string.
int32_t
swift::_swift_stdlib_unicode_compare_utf8_utf8(const unsigned char *LeftString,
                                               int32_t LeftLength,
                                               const unsigned char *RightString,
                                               int32_t RightLength) {
  UCharIterator LeftIterator;
  UCharIterator RightIterator;
  UErrorCode ErrorCode = U_ZERO_ERROR;

  uiter_setUTF8(&LeftIterator, reinterpret_cast<const char *>(LeftString), LeftLength);
  uiter_setUTF8(&RightIterator, reinterpret_cast<const char *>(RightString), RightLength);

  uint32_t Diff = ucol_strcollIter(GetRootCollator(),
    &LeftIterator, &RightIterator, &ErrorCode);
  if (U_FAILURE(ErrorCode)) {
    swift::crash("ucol_strcollIter: Unexpected error doing utf8<->utf8 string comparison.");
  }
  return Diff;
}

void *swift::_swift_stdlib_unicodeCollationIterator_create(
    const __swift_uint16_t *Str, __swift_uint32_t Length) {
  UErrorCode ErrorCode = U_ZERO_ERROR;
#if defined(__CYGWIN__) || defined(_MSC_VER)
  UCollationElements *CollationIterator = ucol_openElements(
    Collator, reinterpret_cast<const UChar *>(Str), Length, ErrorCode);
#else
  UCollationElements *CollationIterator = ucol_openElements(
    Collator, Str, Length, ErrorCode);
#endif
  if (U_FAILURE(ErrorCode)) {
    swift::crash("_swift_stdlib_unicodeCollationIterator_create: ucol_openElements() failed.");
  }
  return CollationIterator;
}

SWIFT_RUNTIME_STDLIB_INTERFACE
__swift_intptr_t swift::_swift_stdlib_unicodeCollationIterator_next(
    void *CollationIterator) {
}

SWIFT_RUNTIME_STDLIB_INTERFACE
void *swift::_swift_stdlib_unicodeCollationIterator_delete(
    void *CollationIterator) {
  ucol_closeElements(CollationIterator);
}

// These functions use murmurhash2 in its 32 and 64bit forms, which are
// differentiated by the constants defined below. This seems like a good choice
// for now because it operates efficiently in blocks rather than bytes, and 
// the data returned from the collation iterator comes in 4byte chunks.
#if __arm__ || __i386__
#define HASH_SEED 0x88ddcc21
#define HASH_M 0x5bd1e995
#define HASH_R 24
#else
#define HASH_SEED 0x429b126688ddcc21
#define HASH_M 0xc6a4a7935bd1e995
#define HASH_R 47
#endif

static intptr_t hashChunk(const UCollator *Collator, intptr_t HashState,
                          const uint16_t *Str, uint32_t Length,
                          UErrorCode *ErrorCode) {
#if defined(__CYGWIN__) || defined(_MSC_VER)
  UCollationElements *CollationIterator = ucol_openElements(
    Collator, reinterpret_cast<const UChar *>(Str), Length, ErrorCode);
#else
  UCollationElements *CollationIterator = ucol_openElements(
    Collator, Str, Length, ErrorCode);
#endif
  while (U_SUCCESS(*ErrorCode)) {
    intptr_t Elem = ucol_next(CollationIterator, ErrorCode);
    // Ignore zero valued collation elements. They don't participate in the
    // ordering relation.
    if (Elem == 0)
      continue;
    if (Elem != UCOL_NULLORDER) {
      Elem *= HASH_M;
      Elem ^= Elem >> HASH_R;
      Elem *= HASH_M;

      HashState *= HASH_M;
      HashState ^= Elem;
    } else {
      break;
    }
  }
  ucol_closeElements(CollationIterator);
  return HashState;
}

static intptr_t hashFinish(intptr_t HashState) {
  HashState ^= HashState >> HASH_R;
  HashState *= HASH_M;
  HashState ^= HashState >> HASH_R;
  return HashState;
}

intptr_t
swift::_swift_stdlib_unicode_hash(const uint16_t *Str, int32_t Length) {
  UErrorCode ErrorCode = U_ZERO_ERROR;
  intptr_t HashState = HASH_SEED;
  HashState = hashChunk(GetRootCollator(), HashState, Str, Length, &ErrorCode);

  if (U_FAILURE(ErrorCode)) {
    swift::crash("hashChunk: Unexpected error hashing unicode string.");
  }
  return hashFinish(HashState);
}

intptr_t swift::_swift_stdlib_unicode_hash_ascii(const unsigned char *Str,
                                                 int32_t Length) {
  const ASCIICollation *Table = ASCIICollation::getTable();
  intptr_t HashState = HASH_SEED;
  int32_t Pos = 0;
  while (Pos < Length) {
    const unsigned char c = Str[Pos++];
    assert((c & 0x80) == 0 && "This table only exists for the ASCII subset");
    intptr_t Elem = Table->map(c);
    // Ignore zero valued collation elements. They don't participate in the
    // ordering relation.
    if (Elem == 0)
      continue;
    Elem *= HASH_M;
    Elem ^= Elem >> HASH_R;
    Elem *= HASH_M;

    HashState *= HASH_M;
    HashState ^= Elem;
  }
  return hashFinish(HashState);
}

/// Convert the unicode string to uppercase. This function will return the
/// required buffer length as a result. If this length does not match the
/// 'DestinationCapacity' this function must be called again with a buffer of
/// the required length to get an uppercase version of the string.
int32_t
swift::_swift_stdlib_unicode_strToUpper(uint16_t *Destination,
                                        int32_t DestinationCapacity,
                                        const uint16_t *Source,
                                        int32_t SourceLength) {
  UErrorCode ErrorCode = U_ZERO_ERROR;
#if defined(__CYGWIN__) || defined(_MSC_VER)
  uint32_t OutputLength = u_strToUpper(reinterpret_cast<UChar *>(Destination),
                                       DestinationCapacity,
                                       reinterpret_cast<const UChar *>(Source),
                                       SourceLength,
                                       "", &ErrorCode);
#else
  uint32_t OutputLength = u_strToUpper(Destination, DestinationCapacity,
                                       Source, SourceLength,
                                       "", &ErrorCode);
#endif
  if (U_FAILURE(ErrorCode) && ErrorCode != U_BUFFER_OVERFLOW_ERROR) {
    swift::crash("u_strToUpper: Unexpected error uppercasing unicode string.");
  }
  return OutputLength;
}

/// Convert the unicode string to lowercase. This function will return the
/// required buffer length as a result. If this length does not match the
/// 'DestinationCapacity' this function must be called again with a buffer of
/// the required length to get a lowercase version of the string.
int32_t
swift::_swift_stdlib_unicode_strToLower(uint16_t *Destination,
                                        int32_t DestinationCapacity,
                                        const uint16_t *Source,
                                        int32_t SourceLength) {
  UErrorCode ErrorCode = U_ZERO_ERROR;
#if defined(__CYGWIN__) || defined(_MSC_VER)
  uint32_t OutputLength = u_strToLower(reinterpret_cast<UChar *>(Destination),
                                       DestinationCapacity,
                                       reinterpret_cast<const UChar *>(Source),
                                       SourceLength,
                                       "", &ErrorCode);
#else
  uint32_t OutputLength = u_strToLower(Destination, DestinationCapacity,
                                       Source, SourceLength,
                                       "", &ErrorCode);
#endif
  if (U_FAILURE(ErrorCode) && ErrorCode != U_BUFFER_OVERFLOW_ERROR) {
    swift::crash("u_strToLower: Unexpected error lowercasing unicode string.");
  }
  return OutputLength;
}

swift::Lazy<ASCIICollation> ASCIICollation::theTable;
