/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* ES6 draft 2014-10-14 23.1.3.5. */

function MapForEach(callbackfn, thisArg = undefined) {
    /* Step 1-2. */
    var M = this;
    if (!IsObject(M))
        ThrowError(JSMSG_BAD_TYPE, typeof M);

    /* Step 3-4. */
    try {
        std_Map_has.call(M);
    } catch (e) {
        ThrowError(JSMSG_BAD_TYPE, typeof M);
    }

    /* Step 5. */
    if (!IsCallable(callbackfn))
        ThrowError(JSMSG_NOT_FUNCTION, DecompileArg(0, callbackfn));

    /* Step 6-8. */
    var entries = std_Map_iterator.call(M);
    while (true) {
        try {
            var entry = std_Map_iterator_next.call(entries);
        } catch (err) {
            if (err instanceof StopIteration)
                break;
            throw err;
        }
        callFunction(callbackfn, thisArg, entry[1], entry[0], M);
    }
}
