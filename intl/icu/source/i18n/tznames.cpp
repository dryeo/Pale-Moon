/*
*******************************************************************************
* Copyright (C) 2011-2012, International Business Machines Corporation and    *
* others. All Rights Reserved.                                                *
*******************************************************************************
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/locid.h"
#include "unicode/tznames.h"
#include "unicode/uenum.h"
#include "cmemory.h"
#include "cstring.h"
#include "putilimp.h"
#include "tznames_impl.h"
#include "uassert.h"
#include "ucln_in.h"
#include "uhash.h"
#include "umutex.h"
#include "uvector.h"


U_NAMESPACE_BEGIN

static const UChar gEtcPrefix[]         = { 0x45, 0x74, 0x63, 0x2F }; // "Etc/"
static const int32_t gEtcPrefixLen      = 4;
static const UChar gSystemVPrefix[]     = { 0x53, 0x79, 0x73, 0x74, 0x65, 0x6D, 0x56, 0x2F }; // "SystemV/
static const int32_t gSystemVPrefixLen  = 8;
static const UChar gRiyadh8[]           = { 0x52, 0x69, 0x79, 0x61, 0x64, 0x68, 0x38 }; // "Riyadh8"
static const int32_t gRiyadh8Len       = 7;

// TimeZoneNames object cache handling
static UMutex gTimeZoneNamesLock = U_MUTEX_INITIALIZER;
static UHashtable *gTimeZoneNamesCache = NULL;
static UBool gTimeZoneNamesCacheInitialized = FALSE;

// Access count - incremented every time up to SWEEP_INTERVAL,
// then reset to 0
static int32_t gAccessCount = 0;

// Interval for calling the cache sweep function - every 100 times
#define SWEEP_INTERVAL 100

// Cache expiration in millisecond. When a cached entry is no
// longer referenced and exceeding this threshold since last
// access time, then the cache entry will be deleted by the sweep
// function. For now, 3 minutes.
#define CACHE_EXPIRATION 180000.0

typedef struct TimeZoneNamesCacheEntry {
    TimeZoneNames*  names;
    int32_t         refCount;
    double          lastAccess;
} TimeZoneNamesCacheEntry;

U_CDECL_BEGIN
/**
 * Cleanup callback func
 */
static UBool U_CALLCONV timeZoneNames_cleanup(void)
{
    if (gTimeZoneNamesCache != NULL) {
        uhash_close(gTimeZoneNamesCache);
        gTimeZoneNamesCache = NULL;
    }
    gTimeZoneNamesCacheInitialized = FALSE;
    return TRUE;
}

/**
 * Deleter for TimeZoneNamesCacheEntry
 */
static void U_CALLCONV
deleteTimeZoneNamesCacheEntry(void *obj) {
    icu::TimeZoneNamesCacheEntry *entry = (icu::TimeZoneNamesCacheEntry*)obj;
    delete (icu::TimeZoneNamesImpl*) entry->names;
    uprv_free(entry);
}
U_CDECL_END

/**
 * Function used for removing unreferrenced cache entries exceeding
 * the expiration time. This function must be called with in the mutex
 * block.
 */
static void sweepCache() {
    int32_t pos = -1;
    const UHashElement* elem;
    double now = (double)uprv_getUTCtime();

    while ((elem = uhash_nextElement(gTimeZoneNamesCache, &pos))) {
        TimeZoneNamesCacheEntry *entry = (TimeZoneNamesCacheEntry *)elem->value.pointer;
        if (entry->refCount <= 0 && (now - entry->lastAccess) > CACHE_EXPIRATION) {
            // delete this entry
            uhash_removeElement(gTimeZoneNamesCache, elem);
        }
    }
}

// ---------------------------------------------------
// TimeZoneNamesDelegate
// ---------------------------------------------------
class TimeZoneNamesDelegate : public TimeZoneNames {
public:
    TimeZoneNamesDelegate(const Locale& locale, UErrorCode& status);
    virtual ~TimeZoneNamesDelegate();

    virtual UBool operator==(const TimeZoneNames& other) const;
    virtual UBool operator!=(const TimeZoneNames& other) const {return !operator==(other);};
    virtual TimeZoneNames* clone() const;

    StringEnumeration* getAvailableMetaZoneIDs(UErrorCode& status) const;
    StringEnumeration* getAvailableMetaZoneIDs(const UnicodeString& tzID, UErrorCode& status) const;
    UnicodeString& getMetaZoneID(const UnicodeString& tzID, UDate date, UnicodeString& mzID) const;
    UnicodeString& getReferenceZoneID(const UnicodeString& mzID, const char* region, UnicodeString& tzID) const;

    UnicodeString& getMetaZoneDisplayName(const UnicodeString& mzID, UTimeZoneNameType type, UnicodeString& name) const;
    UnicodeString& getTimeZoneDisplayName(const UnicodeString& tzID, UTimeZoneNameType type, UnicodeString& name) const;

    UnicodeString& getExemplarLocationName(const UnicodeString& tzID, UnicodeString& name) const;

    MatchInfoCollection* find(const UnicodeString& text, int32_t start, uint32_t types, UErrorCode& status) const;
private:
    TimeZoneNamesDelegate();
    TimeZoneNamesCacheEntry*    fTZnamesCacheEntry;
};

TimeZoneNamesDelegate::TimeZoneNamesDelegate()
: fTZnamesCacheEntry(0) {
}

TimeZoneNamesDelegate::TimeZoneNamesDelegate(const Locale& locale, UErrorCode& status) {
    UBool initialized;
    UMTX_CHECK(&gTimeZoneNamesLock, gTimeZoneNamesCacheInitialized, initialized);
    if (!initialized) {
        // Create empty hashtable
        umtx_lock(&gTimeZoneNamesLock);
        {
            if (!gTimeZoneNamesCacheInitialized) {
                gTimeZoneNamesCache = uhash_open(uhash_hashChars, uhash_compareChars, NULL, &status);
                if (U_SUCCESS(status)) {
                    uhash_setKeyDeleter(gTimeZoneNamesCache, uprv_free);
                    uhash_setValueDeleter(gTimeZoneNamesCache, deleteTimeZoneNamesCacheEntry);
                    gTimeZoneNamesCacheInitialized = TRUE;
                    ucln_i18n_registerCleanup(UCLN_I18N_TIMEZONENAMES, timeZoneNames_cleanup);
                }
            }
        }
        umtx_unlock(&gTimeZoneNamesLock);

        if (U_FAILURE(status)) {
            return;
        }
    }

    // Check the cache, if not available, create new one and cache
    TimeZoneNamesCacheEntry *cacheEntry = NULL;
    umtx_lock(&gTimeZoneNamesLock);
    {
        const char *key = locale.getName();
        cacheEntry = (TimeZoneNamesCacheEntry *)uhash_get(gTimeZoneNamesCache, key);
        if (cacheEntry == NULL) {
            TimeZoneNames *tznames = NULL;
            char *newKey = NULL;

            tznames = new TimeZoneNamesImpl(locale, status);
            if (tznames == NULL) {
                status = U_MEMORY_ALLOCATION_ERROR;
            }
            if (U_SUCCESS(status)) {
                newKey = (char *)uprv_malloc(uprv_strlen(key) + 1);
                if (newKey == NULL) {
                    status = U_MEMORY_ALLOCATION_ERROR;
                } else {
                    uprv_strcpy(newKey, key);
                }
            }
            if (U_SUCCESS(status)) {
                cacheEntry = (TimeZoneNamesCacheEntry *)uprv_malloc(sizeof(TimeZoneNamesCacheEntry));
                if (cacheEntry == NULL) {
                    status = U_MEMORY_ALLOCATION_ERROR;
                } else {
                    cacheEntry->names = tznames;
                    cacheEntry->refCount = 1;
                    cacheEntry->lastAccess = (double)uprv_getUTCtime();

                    uhash_put(gTimeZoneNamesCache, newKey, cacheEntry, &status);
                }
            }
            if (U_FAILURE(status)) {
                if (tznames != NULL) {
                    delete tznames;
                }
                if (newKey != NULL) {
                    uprv_free(newKey);
                }
                if (cacheEntry != NULL) {
                    uprv_free(cacheEntry);
                }
                cacheEntry = NULL;
            }
        } else {
            // Update the reference count
            cacheEntry->refCount++;
            cacheEntry->lastAccess = (double)uprv_getUTCtime();
        }
        gAccessCount++;
        if (gAccessCount >= SWEEP_INTERVAL) {
            // sweep
            sweepCache();
            gAccessCount = 0;
        }
    }
    umtx_unlock(&gTimeZoneNamesLock);

    fTZnamesCacheEntry = cacheEntry;
}

TimeZoneNamesDelegate::~TimeZoneNamesDelegate() {
    umtx_lock(&gTimeZoneNamesLock);
    {
        if (fTZnamesCacheEntry) {
            U_ASSERT(fTZnamesCacheEntry->refCount > 0);
            // Just decrement the reference count
            fTZnamesCacheEntry->refCount--;
        }
    }
    umtx_unlock(&gTimeZoneNamesLock);
}

UBool
TimeZoneNamesDelegate::operator==(const TimeZoneNames& other) const {
    if (this == &other) {
        return TRUE;
    }
    // Just compare if the other object also use the same
    // cache entry
    const TimeZoneNamesDelegate* rhs = dynamic_cast<const TimeZoneNamesDelegate*>(&other);
    if (rhs) {
        return fTZnamesCacheEntry == rhs->fTZnamesCacheEntry;
    }
    return FALSE;
}

TimeZoneNames*
TimeZoneNamesDelegate::clone() const {
    TimeZoneNamesDelegate* other = new TimeZoneNamesDelegate();
    if (other != NULL) {
        umtx_lock(&gTimeZoneNamesLock);
        {
            // Just increment the reference count
            fTZnamesCacheEntry->refCount++;
            other->fTZnamesCacheEntry = fTZnamesCacheEntry;
        }
        umtx_unlock(&gTimeZoneNamesLock);
    }
    return other;
}

StringEnumeration*
TimeZoneNamesDelegate::getAvailableMetaZoneIDs(UErrorCode& status) const {
    return fTZnamesCacheEntry->names->getAvailableMetaZoneIDs(status);
}

StringEnumeration*
TimeZoneNamesDelegate::getAvailableMetaZoneIDs(const UnicodeString& tzID, UErrorCode& status) const {
    return fTZnamesCacheEntry->names->getAvailableMetaZoneIDs(tzID, status);
}

UnicodeString&
TimeZoneNamesDelegate::getMetaZoneID(const UnicodeString& tzID, UDate date, UnicodeString& mzID) const {
    return fTZnamesCacheEntry->names->getMetaZoneID(tzID, date, mzID);
}

UnicodeString&
TimeZoneNamesDelegate::getReferenceZoneID(const UnicodeString& mzID, const char* region, UnicodeString& tzID) const {
    return fTZnamesCacheEntry->names->getReferenceZoneID(mzID, region, tzID);
}

UnicodeString&
TimeZoneNamesDelegate::getMetaZoneDisplayName(const UnicodeString& mzID, UTimeZoneNameType type, UnicodeString& name) const {
    return fTZnamesCacheEntry->names->getMetaZoneDisplayName(mzID, type, name);
}

UnicodeString&
TimeZoneNamesDelegate::getTimeZoneDisplayName(const UnicodeString& tzID, UTimeZoneNameType type, UnicodeString& name) const {
    return fTZnamesCacheEntry->names->getTimeZoneDisplayName(tzID, type, name);
}

UnicodeString&
TimeZoneNamesDelegate::getExemplarLocationName(const UnicodeString& tzID, UnicodeString& name) const {
    return fTZnamesCacheEntry->names->getExemplarLocationName(tzID, name);
}

TimeZoneNames::MatchInfoCollection*
TimeZoneNamesDelegate::find(const UnicodeString& text, int32_t start, uint32_t types, UErrorCode& status) const {
    return fTZnamesCacheEntry->names->find(text, start, types, status);
}

// ---------------------------------------------------
// TimeZoneNames base class
// ---------------------------------------------------
UOBJECT_DEFINE_NO_RTTI_IMPLEMENTATION(TimeZoneNames)

TimeZoneNames::~TimeZoneNames() {
}

TimeZoneNames*
TimeZoneNames::createInstance(const Locale& locale, UErrorCode& status) {
    return new TimeZoneNamesDelegate(locale, status);
}

UnicodeString&
TimeZoneNames::getExemplarLocationName(const UnicodeString& tzID, UnicodeString& name) const {
    if (tzID.isEmpty() || tzID.startsWith(gEtcPrefix, gEtcPrefixLen)
        || tzID.startsWith(gSystemVPrefix, gSystemVPrefixLen) || tzID.indexOf(gRiyadh8, gRiyadh8Len, 0) > 0) {
        name.setToBogus();
        return name;
    }

    int32_t sep = tzID.lastIndexOf((UChar)0x2F /* '/' */);
    if (sep > 0 && sep + 1 < tzID.length()) {
        name.setTo(tzID, sep + 1);
        name.findAndReplace(UnicodeString((UChar)0x5f /* _ */),
                            UnicodeString((UChar)0x20 /* space */));
    } else {
        name.setToBogus();
    }
    return name;
}

UnicodeString&
TimeZoneNames::getDisplayName(const UnicodeString& tzID, UTimeZoneNameType type, UDate date, UnicodeString& name) const {
    getTimeZoneDisplayName(tzID, type, name);
    if (name.isEmpty()) {
        UnicodeString mzID;
        getMetaZoneID(tzID, date, mzID);
        getMetaZoneDisplayName(mzID, type, name);
    }
    return name;
}


struct MatchInfo : UMemory {
    UTimeZoneNameType nameType;
    UnicodeString id;
    int32_t matchLength;
    UBool isTZID;

    MatchInfo(UTimeZoneNameType nameType, int32_t matchLength, const UnicodeString* tzID, const UnicodeString* mzID) {
        this->nameType = nameType;
        this->matchLength = matchLength;
        if (tzID != NULL) {
            this->id.setTo(*tzID);
            this->isTZID = TRUE;
        } else {
            this->id.setTo(*mzID);
            this->isTZID = FALSE;
        }
    }
};

U_CDECL_BEGIN
static void U_CALLCONV
deleteMatchInfo(void *obj) {
    delete static_cast<MatchInfo *>(obj);
}
U_CDECL_END

// ---------------------------------------------------
// MatchInfoCollection class
// ---------------------------------------------------
TimeZoneNames::MatchInfoCollection::MatchInfoCollection()
: fMatches(NULL) {
}

TimeZoneNames::MatchInfoCollection::~MatchInfoCollection() {
    if (fMatches != NULL) {
        delete fMatches;
    }
}

void
TimeZoneNames::MatchInfoCollection::addZone(UTimeZoneNameType nameType, int32_t matchLength,
            const UnicodeString& tzID, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return;
    }
    MatchInfo* matchInfo = new MatchInfo(nameType, matchLength, &tzID, NULL);
    if (matchInfo == NULL) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return;
    }
    matches(status)->addElement(matchInfo, status);
    if (U_FAILURE(status)) {
        delete matchInfo;
    }
}

void
TimeZoneNames::MatchInfoCollection::addMetaZone(UTimeZoneNameType nameType, int32_t matchLength,
            const UnicodeString& mzID, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return;
    }
    MatchInfo* matchInfo = new MatchInfo(nameType, matchLength, NULL, &mzID);
    if (matchInfo == NULL) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return;
    }
    matches(status)->addElement(matchInfo, status);
    if (U_FAILURE(status)) {
        delete matchInfo;
    }
}

int32_t
TimeZoneNames::MatchInfoCollection::size() const {
    if (fMatches == NULL) {
        return 0;
    }
    return fMatches->size();
}

UTimeZoneNameType
TimeZoneNames::MatchInfoCollection::getNameTypeAt(int32_t idx) const {
    const MatchInfo* match = (const MatchInfo*)fMatches->elementAt(idx);
    if (match) {
        return match->nameType;
    }
    return UTZNM_UNKNOWN;
}

int32_t
TimeZoneNames::MatchInfoCollection::getMatchLengthAt(int32_t idx) const {
    const MatchInfo* match = (const MatchInfo*)fMatches->elementAt(idx);
    if (match) {
        return match->matchLength;
    }
    return 0;
}

UBool
TimeZoneNames::MatchInfoCollection::getTimeZoneIDAt(int32_t idx, UnicodeString& tzID) const {
    tzID.remove();
    const MatchInfo* match = (const MatchInfo*)fMatches->elementAt(idx);
    if (match && match->isTZID) {
        tzID.setTo(match->id);
        return TRUE;
    }
    return FALSE;
}

UBool
TimeZoneNames::MatchInfoCollection::getMetaZoneIDAt(int32_t idx, UnicodeString& mzID) const {
    mzID.remove();
    const MatchInfo* match = (const MatchInfo*)fMatches->elementAt(idx);
    if (match && !match->isTZID) {
        mzID.setTo(match->id);
        return TRUE;
    }
    return FALSE;
}

UVector*
TimeZoneNames::MatchInfoCollection::matches(UErrorCode& status) {
    if (U_FAILURE(status)) {
        return NULL;
    }
    if (fMatches != NULL) {
        return fMatches;
    }
    fMatches = new UVector(deleteMatchInfo, NULL, status);
    if (fMatches == NULL) {
        status = U_MEMORY_ALLOCATION_ERROR;
    } else if (U_FAILURE(status)) {
        delete fMatches;
        fMatches = NULL;
    }
    return fMatches;
}


U_NAMESPACE_END
#endif
