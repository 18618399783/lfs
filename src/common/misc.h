#ifndef MISC_H
#define MISC_H

/*
 *  * min()/max()/clamp() macros that also do
 *   * strict type-checking.. See the
 *    * "unnecessary" pointer comparison.
 *     */
#define min(x, y) ({                \
    typeof(x) _min1 = (x);          \
    typeof(y) _min2 = (y);          \
    (void) (&_min1 == &_min2);      \
    _min1 < _min2 ? _min1 : _min2; })

#define max(x, y) ({                \
    typeof(x) _max1 = (x);          \
    typeof(y) _max2 = (y);          \
    (void) (&_max1 == &_max2);      \
    _max1 > _max2 ? _max1 : _max2; })

/**
 *  * clamp - return a value clamped to a given range with strict typechecking
 *   * @val: current value
 *    * @min: minimum allowable value
 *     * @max: maximum allowable value
 *      *
 *       * This macro does strict typechecking of min/max to make sure they are of the
 *        * same type as val.  See the unnecessary pointer comparisons.
 *         */
#define clamp(val, min, max) ({         \
    typeof(val) __val = (val);      \
    typeof(min) __min = (min);      \
    typeof(max) __max = (max);      \
    (void) (&__val == &__min);      \
    (void) (&__val == &__max);      \
    __val = __val < __min ? __min: __val;   \
    __val > __max ? __max: __val; })

/*
 *  * ..and if you can't take the strict
 *   * types, you can specify one yourself.
 *    *
 *     * Or not use min/max/clamp at all, of course.
 *      */
#define min_t(type, x, y) ({            \
    type __min1 = (x);          \
    type __min2 = (y);          \
    __min1 < __min2 ? __min1: __min2; })

#define max_t(type, x, y) ({            \
    type __max1 = (x);          \
    type __max2 = (y);          \
    __max1 > __max2 ? __max1: __max2; })

/**
 *  * clamp_t - return a value clamped to a given range using a given type
 *   * @type: the type of variable to use
 *    * @val: current value
 *     * @min: minimum allowable value
 *      * @max: maximum allowable value
 *       *
 *        * This macro does no typechecking and uses temporary variables of type
 *         * 'type' to make all the comparisons.
 *          */
#define clamp_t(type, val, min, max) ({     \
    type __val = (val);         \
    type __min = (min);         \
    type __max = (max);         \
    __val = __val < __min ? __min: __val;   \
    __val > __max ? __max: __val; })

/**
 *  * clamp_val - return a value clamped to a given range using val's type
 *   * @val: current value
 *    * @min: minimum allowable value
 *     * @max: maximum allowable value
 *      *
 *       * This macro does no typechecking and uses temporary variables of whatever
 *        * type the input argument 'val' is.  This is useful when val is an unsigned
 *         * type and min and max are literals that will otherwise be assigned a signed
 *          * integer type.
 *           */
#define clamp_val(val, min, max) ({     \
    typeof(val) __val = (val);      \
    typeof(val) __min = (min);      \
    typeof(val) __max = (max);      \
    __val = __val < __min ? __min: __val;   \
    __val > __max ? __max: __val; })


/*
 *  * swap - swap value of @a and @b
 *   */
#define swap(a, b) \
    do { typeof(a) __tmp = (a); (a) = (b); (b) = __tmp; } while (0)

/**
 *  * container_of - cast a member of a structure out to the containing structure
 *   * @ptr:    the pointer to the member.
 *    * @type:  the type of the container struct this is embedded in.
 *     * @member:   the name of the member within the struct.
 *      *
 *       */
#define container_of(ptr, type, member) ({          \
    const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
    (type *)( (char *)__mptr - offsetof(type,member) );})


#endif
