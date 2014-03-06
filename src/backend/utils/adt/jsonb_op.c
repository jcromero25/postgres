/*-------------------------------------------------------------------------
 *
 * jsonb_op.c
 *	 Operators for jsonb common to multiple access methods
 *
 * Copyright (c) 2014, PostgreSQL Global Development Group
 *
 *
 * IDENTIFICATION
 *	  src/backend/utils/adt/jsonb_op.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/hash.h"
#include "catalog/pg_type.h"
#include "funcapi.h"
#include "utils/jsonb.h"
#include "utils/memutils.h"

static JsonbValue*
arrayToJsonbSortedArray(ArrayType *a)
{
	Datum	 		*key_datums;
	bool	 		*key_nulls;
	int				key_count;
	JsonbValue		*v;
	int				i,
					j;
	bool			hasNonUniq = false;

	deconstruct_array(a,
					  TEXTOID, -1, false, 'i',
					  &key_datums, &key_nulls, &key_count);

	if (key_count == 0)
		return NULL;

	/*
	 * A text array uses at least eight bytes per element, so any overflow in
	 * "key_count * sizeof(JsonbPair)" is small enough for palloc() to catch.
	 * However, credible improvements to the array format could invalidate
	 * that assumption.  Therefore, use an explicit check rather than relying
	 * on palloc() to complain.
	 */
	if (key_count > MaxAllocSize / sizeof(JsonbPair))
		ereport(ERROR,
				(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
			  errmsg("number of pairs (%d) exceeds the maximum allowed (%d)",
					 key_count, (int) (MaxAllocSize / sizeof(JsonbPair)))));

	v = palloc(sizeof(*v));
	v->type = jbvArray;
	v->array.scalar = false;
	v->array.elems = palloc(sizeof(*v->hash.pairs) * key_count);

	for (i = 0, j = 0; i < key_count; i++)
	{
		if (!key_nulls[i])
		{
			v->array.elems[j].type = jbvString;
			v->array.elems[j].string.val = VARDATA(key_datums[i]);
			v->array.elems[j].string.len = VARSIZE(key_datums[i]) - VARHDRSZ;
			j++;
		}
	}
	v->array.nelems = j;

	if (v->array.nelems > 1)
		qsort_arg(v->array.elems, v->array.nelems, sizeof(*v->array.elems),
				  compareJsonbStringValue /* compareJsonbStringValue */, &hasNonUniq);

	if (hasNonUniq)
	{
		JsonbValue	*ptr = v->array.elems + 1,
					*res = v->array.elems;

		while (ptr - v->array.elems < v->array.nelems)
		{
			if (!(ptr->string.len == res->string.len &&
				  memcmp(ptr->string.val, res->string.val, ptr->string.len) == 0))
			{
				res++;
				*res = *ptr;
			}

			ptr++;
		}

		v->array.nelems = res + 1 - v->array.elems;
	}

	return v;
}

Datum
jsonb_exists(PG_FUNCTION_ARGS)
{
	Jsonb	   	*hs = PG_GETARG_JSONB(0);
	text		*key = PG_GETARG_TEXT_PP(1);
	JsonbValue	*v = NULL;

	if (!JB_ISEMPTY(hs))
		v = findUncompressedJsonbValue(VARDATA(hs),
										JB_FLAG_OBJECT | JB_FLAG_ARRAY,
										NULL,
										VARDATA_ANY(key),
										VARSIZE_ANY_EXHDR(key));

	PG_RETURN_BOOL(v != NULL);
}

Datum
jsonb_exists_any(PG_FUNCTION_ARGS)
{
	Jsonb	   		*hs = PG_GETARG_JSONB(0);
	ArrayType	  	*keys = PG_GETARG_ARRAYTYPE_P(1);
	JsonbValue		*v = arrayToJsonbSortedArray(keys);
	int				i;
	uint32			*plowbound = NULL, lowbound = 0;
	bool			res = false;

	if (JB_ISEMPTY(hs) || v == NULL || v->hash.npairs == 0)
		PG_RETURN_BOOL(false);

	if (JB_ROOT_IS_OBJECT(hs))
		plowbound = &lowbound;
	/*
	 * we exploit the fact that the pairs list is already sorted into strictly
	 * increasing order to narrow the findUncompressedJsonbValue search; each search can
	 * start one entry past the previous "found" entry, or at the lower bound
	 * of the last search.
	 */
	for (i = 0; i < v->array.nelems; i++)
	{
		if (findUncompressedJsonbValueByValue(VARDATA(hs), JB_FLAG_OBJECT | JB_FLAG_ARRAY, plowbound,
											   v->array.elems + i) != NULL)
		{
			res = true;
			break;
		}
	}

	PG_RETURN_BOOL(res);
}

Datum
jsonb_exists_all(PG_FUNCTION_ARGS)
{
	Jsonb	   		*hs = PG_GETARG_JSONB(0);
	ArrayType	  	*keys = PG_GETARG_ARRAYTYPE_P(1);
	JsonbValue		*v = arrayToJsonbSortedArray(keys);
	int				i;
	uint32			*plowbound = NULL, lowbound = 0;
	bool			res = false;

	if (JB_ISEMPTY(hs) || v == NULL || v->hash.npairs == 0)
		PG_RETURN_BOOL(false);

	if (JB_ROOT_IS_OBJECT(hs))
		plowbound = &lowbound;
	/*
	 * we exploit the fact that the pairs list is already sorted into strictly
	 * increasing order to narrow the findUncompressedJsonbValue search; each search can
	 * start one entry past the previous "found" entry, or at the lower bound
	 * of the last search.
	 */
	for (i = 0; i < v->array.nelems; i++)
	{
		if (findUncompressedJsonbValueByValue(VARDATA(hs), JB_FLAG_OBJECT | JB_FLAG_ARRAY, plowbound,
											   v->array.elems + i) != NULL)
		{
			res = true;
			break;
		}
	}

	PG_RETURN_BOOL(res);
}

/*
 * Common initialization function for the various set-returning funcs.
 *
 * fcinfo is only passed if the function is to return a composite; it will be
 * used to look up the return tupledesc.  we stash a copy of the jsonb in the
 * multi-call context in case it was originally toasted.  (At least I assume
 * that's why; there was no explanatory comment in the original code. --AG)
 */
typedef struct SetReturningState
{
	Jsonb			*hs;
	JsonbIterator	*it;
	MemoryContext	ctx;

	JsonbValue		init;
	int				path_len;
	int				level;
	struct {
		JsonbValue		v;
		Datum           varStr;
		int				varInt;
		enum {
			pathStr,
			pathInt,
			pathAny
		} 				varKind;
		int				i;
	}				*path;
} SetReturningState;

static bool
deepContains(JsonbIterator **it1, JsonbIterator **it2)
{
	uint32			r1, r2;
	JsonbValue		v1, v2;
	bool			res = true;

	r1 = JsonbIteratorGet(it1, &v1, false);
	r2 = JsonbIteratorGet(it2, &v2, false);

	if (r1 != r2)
	{
		res = false;
	}
	else if (r1 == WJB_BEGIN_OBJECT)
	{
		uint32		lowbound = 0;
		JsonbValue	*v;

		for(;;) {
			r2 = JsonbIteratorGet(it2, &v2, false);
			if (r2 == WJB_END_OBJECT)
				break;

			Assert(r2 == WJB_KEY);

			v = findUncompressedJsonbValueByValue((*it1)->buffer,
												   JB_FLAG_OBJECT,
												   &lowbound, &v2);

			if (v == NULL)
			{
				res = false;
				break;
			}

			r2 = JsonbIteratorGet(it2, &v2, true);
			Assert(r2 == WJB_VALUE);

			if (v->type != v2.type)
			{
				res = false;
				break;
			}
			else if (v->type == jbvString || v->type == jbvNull ||
					 v->type == jbvBool || v->type == jbvNumeric)
			{
				if (compareJsonbValue(v, &v2) != 0)
				{
					res = false;
					break;
				}
			}
			else
			{
				JsonbIterator	*it1a, *it2a;

				Assert(v2.type == jbvBinary);
				Assert(v->type == jbvBinary);

				it1a = JsonbIteratorInit(v->binary.data);
				it2a = JsonbIteratorInit(v2.binary.data);

				if ((res = deepContains(&it1a, &it2a)) == false)
					break;
			}
		}
	}
	else if (r1 == WJB_BEGIN_ARRAY)
	{
		JsonbValue		*v;
		JsonbValue		*av = NULL;
		uint32			nelems = v1.array.nelems;

		for(;;) {
			r2 = JsonbIteratorGet(it2, &v2, true);
			if (r2 == WJB_END_ARRAY)
				break;

			Assert(r2 == WJB_ELEM);

			if (v2.type == jbvString || v2.type == jbvNull ||
				v2.type == jbvBool || v2.type == jbvNumeric)
			{
				v = findUncompressedJsonbValueByValue((*it1)->buffer,
													   JB_FLAG_ARRAY, NULL,
													   &v2);
				if (v == NULL)
				{
					res = false;
					break;
				}
			}
			else
			{
				uint32 			i;

				if (av == NULL)
				{
					uint32 			j = 0;

					av = palloc(sizeof(*av) * nelems);

					for(i=0; i<nelems; i++)
					{
						r2 = JsonbIteratorGet(it1, &v1, true);
						Assert(r2 == WJB_ELEM);

						if (v1.type == jbvBinary)
							av[j++] = v1;
					}

					if (j == 0)
					{
						res = false;
						break;
					}

					nelems = j;
				}

				res = false;
				for(i = 0; res == false && i<nelems; i++)
				{
					JsonbIterator	*it1a, *it2a;

					it1a = JsonbIteratorInit(av[i].binary.data);
					it2a = JsonbIteratorInit(v2.binary.data);

					res = deepContains(&it1a, &it2a);
				}

				if (res == false)
					break;
			}
		}
	}
	else
	{
		elog(ERROR, "wrong jsonb container type");
	}

	return res;
}

Datum
jsonb_contains(PG_FUNCTION_ARGS)
{
	Jsonb	   		*val = PG_GETARG_JSONB(0);
	Jsonb	   		*tmpl = PG_GETARG_JSONB(1);

	bool			res = true;
	JsonbIterator	*it1, *it2;

	if (JB_ROOT_COUNT(val) < JB_ROOT_COUNT(tmpl) ||
		JB_ROOT_IS_OBJECT(val) != JB_ROOT_IS_OBJECT(tmpl))
		PG_RETURN_BOOL(false);

	it1 = JsonbIteratorInit(VARDATA(val));
	it2 = JsonbIteratorInit(VARDATA(tmpl));
	res = deepContains(&it1, &it2);

	PG_RETURN_BOOL(res);
}

/*
 * Kludge to work around the need for an alternative pg_proc entry for each
 * operator's oprcode procedure
 */
Datum
jsonb_contained_alt(PG_FUNCTION_ARGS)
{
	PG_RETURN_DATUM(DirectFunctionCall2(jsonb_contained,
										PG_GETARG_DATUM(0),
										PG_GETARG_DATUM(1)
										));
}

Datum
jsonb_contains_alt(PG_FUNCTION_ARGS)
{
	PG_RETURN_DATUM(DirectFunctionCall2(jsonb_contains,
										PG_GETARG_DATUM(0),
										PG_GETARG_DATUM(1)
										));
}

Datum
jsonb_contained(PG_FUNCTION_ARGS)
{
	PG_RETURN_DATUM(DirectFunctionCall2(jsonb_contains,
										PG_GETARG_DATUM(1),
										PG_GETARG_DATUM(0)
										));
}

/*
 * B-Tree operator class procedures
 */
Datum
jsonb_cmp(PG_FUNCTION_ARGS)
{
	Jsonb	   		*hs1 = PG_GETARG_JSONB(0);
	Jsonb	   		*hs2 = PG_GETARG_JSONB(1);

	int				res;

	if (JB_ISEMPTY(hs1) || JB_ISEMPTY(hs2))
	{
		if (JB_ISEMPTY(hs1))
		{
			if (JB_ISEMPTY(hs2))
				res = 0;
			else
				res = -1;
		}
		else
		{
			res = 1;
		}
	}
	else
	{
		res = compareJsonbBinaryValue(VARDATA(hs1), VARDATA(hs2));
	}

	/*
	 * this is a btree support function; this is one of the few places where
	 * memory needs to be explicitly freed.
	 */
	PG_FREE_IF_COPY(hs1, 0);
	PG_FREE_IF_COPY(hs2, 1);
	PG_RETURN_INT32(res);
}

Datum
jsonb_eq(PG_FUNCTION_ARGS)
{
	int			res = DatumGetInt32(DirectFunctionCall2(jsonb_cmp,
														PG_GETARG_DATUM(0),
														PG_GETARG_DATUM(1)));

	PG_RETURN_BOOL(res == 0);
}

Datum
jsonb_ne(PG_FUNCTION_ARGS)
{
	int			res = DatumGetInt32(DirectFunctionCall2(jsonb_cmp,
														PG_GETARG_DATUM(0),
														PG_GETARG_DATUM(1)));

	PG_RETURN_BOOL(res != 0);
}

Datum
jsonb_gt(PG_FUNCTION_ARGS)
{
	int			res = DatumGetInt32(DirectFunctionCall2(jsonb_cmp,
														PG_GETARG_DATUM(0),
														PG_GETARG_DATUM(1)));

	PG_RETURN_BOOL(res > 0);
}

Datum
jsonb_ge(PG_FUNCTION_ARGS)
{
	int			res = DatumGetInt32(DirectFunctionCall2(jsonb_cmp,
														PG_GETARG_DATUM(0),
														PG_GETARG_DATUM(1)));

	PG_RETURN_BOOL(res >= 0);
}

Datum
jsonb_lt(PG_FUNCTION_ARGS)
{
	int			res = DatumGetInt32(DirectFunctionCall2(jsonb_cmp,
														PG_GETARG_DATUM(0),
														PG_GETARG_DATUM(1)));

	PG_RETURN_BOOL(res < 0);
}

Datum
jsonb_le(PG_FUNCTION_ARGS)
{
	int			res = DatumGetInt32(DirectFunctionCall2(jsonb_cmp,
														PG_GETARG_DATUM(0),
														PG_GETARG_DATUM(1)));

	PG_RETURN_BOOL(res <= 0);
}

/* Hash operator class hashing function */
Datum
jsonb_hash(PG_FUNCTION_ARGS)
{
	Jsonb	   	*hs = PG_GETARG_JSONB(0);

	Datum		hval = hash_any((unsigned char *) VARDATA(hs),
								VARSIZE(hs) - VARHDRSZ);

	PG_FREE_IF_COPY(hs, 0);
	PG_RETURN_DATUM(hval);
}