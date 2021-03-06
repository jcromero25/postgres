#include "postgres.h"
#include "utils/builtins.h"

#include "hstore.h"

/****************************************************************************
 *                         Compare Functions                                *
 ****************************************************************************/
int
compareHStoreStringValue(const void *a, const void *b, void *arg)
{
	const HStoreValue  *va = a;
	const HStoreValue  *vb = b;
	int					res;

	Assert(va->type == hsvString);
	Assert(vb->type == hsvString);

	if (va->string.len == vb->string.len)
	{
		res = memcmp(va->string.val, vb->string.val, va->string.len);
		if (res == 0 && arg)
			*(bool*)arg = true;
	}
	else
	{
		res = (va->string.len > vb->string.len) ? 1 : -1;
	}

	return res;
}

int
compareHStorePair(const void *a, const void *b, void *arg)
{
	const 	HStorePair *pa = a;
	const 	HStorePair *pb = b;
	int 	res;

	res = compareHStoreStringValue(&pa->key, &pb->key, arg);

	/*
	 * guarantee keeping order of equal pair. Unique algorithm will
	 * prefer first element as value
	 */

	if (res == 0)
		res = (pa->order > pb->order) ? -1 : 1;

	return res;
}

int
compareHStoreValue(HStoreValue *a, HStoreValue *b)
{
	if (a->type == b->type)
	{
		switch(a->type)
		{
			case hsvNull:
				return 0;
			case hsvString:
				return compareHStoreStringValue(a, b, NULL);
			case hsvBool:
				if (a->boolean == b->boolean)
					return 0;
				return (a->boolean > b->boolean) ? 1 : -1;
			case hsvNumeric:
				return DatumGetInt32(DirectFunctionCall2(numeric_cmp,
														 PointerGetDatum(a->numeric),
														 PointerGetDatum(b->numeric)));
			case hsvArray:
				if (a->array.nelems == b->array.nelems)
				{
					int i, r;

					for(i=0; i<a->array.nelems; i++)
						if ((r = compareHStoreValue(a->array.elems + i,
													b->array.elems + i)) != 0)
							return r;

					return 0;
				}

				return (a->array.nelems > b->array.nelems) ? 1 : -1;
			case hsvHash:
				if (a->hash.npairs == b->hash.npairs)
				{
					int i, r;

					for(i=0; i<a->hash.npairs; i++)
					{
						if ((r = compareHStoreStringValue(&a->hash.pairs[i].key,
														  &b->hash.pairs[i].key,
														  NULL)) != 0)
							return r;
						if ((r = compareHStoreValue(&a->hash.pairs[i].value,
													&b->hash.pairs[i].value)) != 0)
							return r;
					}

					return 0;
				}

				return (a->hash.npairs > b->hash.npairs) ? 1 : -1;
			case hsvBinary:
				return compareHStoreBinaryValue(a->binary.data, b->binary.data);
			default:
				elog(PANIC, "unknown HStoreValue->type: %d", a->type);
		}
	}

	return (a->type > b->type) ? 1 : -1;
}

int
compareHStoreBinaryValue(char *a, char *b)
{
	HStoreIterator	*it1, *it2;
	int				res = 0;

	it1 = HStoreIteratorInit(a);
	it2 = HStoreIteratorInit(b);

	while(res == 0)
	{
		HStoreValue		v1, v2;
		int				r1, r2;

		r1 = HStoreIteratorGet(&it1, &v1, false);
		r2 = HStoreIteratorGet(&it2, &v2, false);

		if (r1 == r2)
		{
			if (r1 == 0)
				break; /* equal */

			if (v1.type == v2.type)
			{
				switch(v1.type)
				{
					case hsvString:
						res = compareHStoreStringValue(&v1, &v2, NULL);
						break;
					case hsvBool:
						if (v1.boolean == v2.boolean)
							res = 0;
						else
							res = (v1.boolean > v2.boolean) ? 1 : -1;
						break;
					case hsvNumeric:
						res = DatumGetInt32(DirectFunctionCall2(numeric_cmp,
												PointerGetDatum(v1.numeric),
												PointerGetDatum(v2.numeric)));
						break;
					case hsvArray:
						if (v1.array.nelems != v2.array.nelems)
							res = (v1.array.nelems > v2.array.nelems) ? 1 : -1;
						break;
					case hsvHash:
						if (v1.hash.npairs != v2.hash.npairs)
							res = (v1.hash.npairs > v2.hash.npairs) ? 1 : -1;
						break;
					default:
						break;
				}
			}
			else
			{
				res = (v1.type > v2.type) ?  1 : -1; /* dummy order */
			}
		}
		else
		{
			res = (r1 > r2) ? 1 : -1; /* dummy order */
		}
	}

	return res;
}

/*
 * Sort and unique pairs in hash-like HStoreValue
 */
void
uniqueHStoreValue(HStoreValue *v)
{
	bool	hasNonUniq = false;

	Assert(v->type == hsvHash);

	if (v->hash.npairs > 1)
		qsort_arg(v->hash.pairs, v->hash.npairs, sizeof(*v->hash.pairs),
				  compareHStorePair, &hasNonUniq);

	if (hasNonUniq)
	{
		HStorePair  *ptr = v->hash.pairs + 1,
					*res = v->hash.pairs;

		while(ptr - v->hash.pairs < v->hash.npairs)
		{
			if (ptr->key.string.len == res->key.string.len &&
				memcmp(ptr->key.string.val, res->key.string.val,
					   ptr->key.string.len) == 0)
			{
				v->size -= ptr->key.size + ptr->value.size;
			}
			else
			{
				res++;
				if (ptr != res)
					memcpy(res, ptr, sizeof(*res));
			}
			ptr++;
		}

		v->hash.npairs = res + 1 - v->hash.pairs;
	}
}


/****************************************************************************
 *                         find string key in hash or array                 *
 ****************************************************************************/
HStoreValue*
findUncompressedHStoreValueByValue(char *buffer, uint32 flags, uint32 *lowbound, HStoreValue *key)
{
	uint32				header = *(uint32*)buffer;
	static HStoreValue 	r;

	Assert((header & (HS_FLAG_ARRAY | HS_FLAG_HSTORE)) !=
		   (HS_FLAG_ARRAY | HS_FLAG_HSTORE));

	if (flags & HS_FLAG_ARRAY & header)
	{
		HEntry	*array = (HEntry*)(buffer + sizeof(header));
		char 	*data = (char*)(array + (header & HS_COUNT_MASK));
		int 	i;

		for(i=(lowbound) ? *lowbound : 0; i<(header & HS_COUNT_MASK); i++) {
			HEntry	*e = array + i;

			if (HSE_ISNULL(*e) && key->type == hsvNull)
			{
				r.type = hsvNull;
				if (lowbound)
					*lowbound = i;
				r.size = sizeof(HEntry);

				return &r;
			}
			else if (HSE_ISSTRING(*e) && key->type == hsvString )
			{
				if (key->string.len == HSE_LEN(*e) &&
					memcmp(key->string.val, data + HSE_OFF(*e),
						   key->string.len) == 0)
				{
					r.type = hsvString;
					r.string.val = data + HSE_OFF(*e);
					r.string.len = key->string.len;
					r.size = sizeof(HEntry) + r.string.len;
					if (lowbound)
						*lowbound = i;

					return &r;
				}
			}
			else if (HSE_ISBOOL(*e) && key->type == hsvBool)
			{
				if ((HSE_ISBOOL_TRUE(*e) && key->boolean == true) ||
					(HSE_ISBOOL_FALSE(*e) && key->boolean == false))
				{
					r = *key;
					r.size = sizeof(HEntry);
					if (lowbound)
						*lowbound = i;

					return &r;
				}
			}
			else if (HSE_ISNUMERIC(*e) && key->type == hsvNumeric)
			{
				if (DatumGetBool(DirectFunctionCall2(numeric_eq,
								PointerGetDatum(data + INTALIGN(HSE_OFF(*e))),
								PointerGetDatum(key->numeric))) == true)
				{
					r.type = hsvNumeric;
					r.numeric = (Numeric)(data + INTALIGN(HSE_OFF(*e)));
					if (lowbound)
						*lowbound = i;

					return &r;
				}
			}
		}
	}
	else if (flags & HS_FLAG_HSTORE & header)
	{
		HEntry  *array = (HEntry*)(buffer + sizeof(header));
		char    *data = (char*)(array + (header & HS_COUNT_MASK) * 2);
		uint32	stopLow = lowbound ? *lowbound : 0,
				stopHigh = (header & HS_COUNT_MASK),
				stopMiddle;

		if (key->type != hsvString)
			return NULL;

		while (stopLow < stopHigh)
		{
			int		difference;
			HEntry	*e;

			stopMiddle = stopLow + (stopHigh - stopLow) / 2;

			e = array + stopMiddle * 2;

			if (key->string.len == HSE_LEN(*e))
				difference = memcmp(data + HSE_OFF(*e), key->string.val,
									key->string.len);
			else
				difference = (HSE_LEN(*e) > key->string.len) ? 1 : -1;

			if (difference == 0)
			{
				HEntry	*v = e + 1;

				if (lowbound)
					*lowbound = stopMiddle + 1;

				if (HSE_ISSTRING(*v))
				{
					r.type = hsvString;
					r.string.val = data + HSE_OFF(*v);
					r.string.len = HSE_LEN(*v);
					r.size = sizeof(HEntry) + r.string.len;
				}
				else if (HSE_ISBOOL(*v))
				{
					r.type = hsvBool;
					r.boolean = (HSE_ISBOOL_TRUE(*v)) ? true : false;
					r.size = sizeof(HEntry);
				}
				else if (HSE_ISNUMERIC(*v))
				{
					r.type = hsvNumeric;
					r.numeric = (Numeric)(data + INTALIGN(HSE_OFF(*v)));
					r.size = 2*sizeof(HEntry) + VARSIZE_ANY(r.numeric);
				}
				else if (HSE_ISNULL(*v))
				{
					r.type = hsvNull;
					r.size = sizeof(HEntry);
				}
				else
				{
					r.type = hsvBinary;
					r.binary.data = data + INTALIGN(HSE_OFF(*v));
					r.binary.len = HSE_LEN(*v) -
									(INTALIGN(HSE_OFF(*v)) - HSE_OFF(*v));
					r.size = 2*sizeof(HEntry) + r.binary.len;
				}

				return &r;
			}
			else if (difference < 0)
			{
				stopLow = stopMiddle + 1;
			}
			else
			{
				stopHigh = stopMiddle;
			}
		}

		if (lowbound)
			*lowbound = stopLow;
	}

	return NULL;
}

HStoreValue*
findUncompressedHStoreValue(char *buffer, uint32 flags, uint32 *lowbound,
							char *key, uint32 keylen)
{
	HStoreValue	v;

	if (key == NULL)
	{
		v.type = hsvNull;
	}
	else
	{
		v.type = hsvString;
		v.string.val = key;
		v.string.len = keylen;
	}

	return findUncompressedHStoreValueByValue(buffer, flags, lowbound, &v);
}

HStoreValue*
getHStoreValue(char *buffer, uint32 flags, int32 i)
{
	uint32				header = *(uint32*)buffer;
	static HStoreValue	r;
	HEntry				*array, *e;
	char				*data;

	Assert((header & (HS_FLAG_ARRAY | HS_FLAG_HSTORE)) !=
		   (HS_FLAG_ARRAY | HS_FLAG_HSTORE));

	if (i >= 0)
	{
		if (i >= (header & HS_COUNT_MASK))
			return NULL;
	}
	else
	{
		if (-i > (header & HS_COUNT_MASK))
			return NULL;

		i = (header & HS_COUNT_MASK) + i;
	}

	array = (HEntry*)(buffer + sizeof(header));

	if (flags & HS_FLAG_ARRAY & header)
	{
		e = array + i;
		data = (char*)(array + (header & HS_COUNT_MASK));
	}
	else if (flags & HS_FLAG_HSTORE & header)
	{
		e = array + i * 2 + 1;
		data = (char*)(array + (header & HS_COUNT_MASK) * 2);
	}
	else
	{
		return NULL;
	}

	if (HSE_ISSTRING(*e))
	{
		r.type = hsvString;
		r.string.val = data + HSE_OFF(*e);
		r.string.len = HSE_LEN(*e);
		r.size = sizeof(HEntry) + r.string.len;
	}
	else if (HSE_ISBOOL(*e))
	{
		r.type = hsvBool;
		r.boolean = (HSE_ISBOOL_TRUE(*e)) ? true : false;
		r.size = sizeof(HEntry);
	}
	else if (HSE_ISNUMERIC(*e))
	{
		r.type = hsvNumeric;
		r.numeric = (Numeric)(data + INTALIGN(HSE_OFF(*e)));
		r.size = 2*sizeof(HEntry) + VARSIZE_ANY(r.numeric);
	}
	else if (HSE_ISNULL(*e))
	{
		r.type = hsvNull;
		r.size = sizeof(HEntry);
	}
	else
	{
		r.type = hsvBinary;
		r.binary.data = data + INTALIGN(HSE_OFF(*e));
		r.binary.len = HSE_LEN(*e) - (INTALIGN(HSE_OFF(*e)) - HSE_OFF(*e));
		r.size = r.binary.len + 2*sizeof(HEntry);
	}

	return &r;
}

/****************************************************************************
 *                         Walk on tree representation of hstore            *
 ****************************************************************************/
static void
walkUncompressedHStoreDo(HStoreValue *v, walk_hstore_cb cb, void *cb_arg,
						 uint32 level)
{
	int i;

	switch(v->type)
	{
		case hsvArray:
			cb(cb_arg, v, WHS_BEGIN_ARRAY, level);
			for(i=0; i<v->array.nelems; i++)
			{
				if (v->array.elems[i].type == hsvNull ||
					v->array.elems[i].type == hsvString ||
					v->array.elems[i].type == hsvBool ||
					v->array.elems[i].type == hsvNumeric ||
					v->array.elems[i].type == hsvBinary)
					cb(cb_arg, v->array.elems + i, WHS_ELEM, level);
				else
					walkUncompressedHStoreDo(v->array.elems + i, cb, cb_arg,
											 level + 1);
			}
			cb(cb_arg, v, WHS_END_ARRAY, level);
			break;
		case hsvHash:
			cb(cb_arg, v, WHS_BEGIN_HASH, level);

			for(i=0; i<v->hash.npairs; i++)
			{
				cb(cb_arg, &v->hash.pairs[i].key, WHS_KEY, level);

				if (v->hash.pairs[i].value.type == hsvNull ||
					v->hash.pairs[i].value.type == hsvString ||
					v->hash.pairs[i].value.type == hsvBool ||
					v->hash.pairs[i].value.type == hsvNumeric ||
					v->hash.pairs[i].value.type == hsvBinary)
					cb(cb_arg, &v->hash.pairs[i].value, WHS_VALUE, level);
				else
					walkUncompressedHStoreDo(&v->hash.pairs[i].value, cb, cb_arg, level + 1);
			}

			cb(cb_arg, v, WHS_END_HASH, level);
			break;
		default:
			elog(PANIC, "impossible HStoreValue->type: %d", v->type);
	}
}

void
walkUncompressedHStore(HStoreValue *v, walk_hstore_cb cb, void *cb_arg)
{
	if (v)
		walkUncompressedHStoreDo(v, cb, cb_arg, 0);
}

/****************************************************************************
 *                         Iteration over binary hstore                     *
 ****************************************************************************/
static void
parseBuffer(HStoreIterator *it, char *buffer)
{
	uint32	header = *(uint32*)buffer;

	it->type = header & (HS_FLAG_ARRAY | HS_FLAG_HSTORE);
	it->nelems = header & HS_COUNT_MASK;
	it->buffer = buffer;


	buffer += sizeof(uint32);
	it->array = (HEntry*)buffer;

	it->state = hsi_start;

	switch(it->type)
	{
		case HS_FLAG_ARRAY:
			it->data = buffer + it->nelems * sizeof(HEntry);
			it->isScalar = (header & HS_FLAG_SCALAR) ? true : false;
			Assert(it->isScalar == false || it->nelems == 1);
			break;
		case HS_FLAG_HSTORE:
			it->data = buffer + it->nelems * sizeof(HEntry) * 2;
			break;
		default:
			elog(PANIC, "impossible type: %08x", it->type);
	}
}

HStoreIterator*
HStoreIteratorInit(char *buffer)
{
	HStoreIterator	*it = palloc(sizeof(*it));

	parseBuffer(it, buffer);
	it->next = NULL;

	return it;
}

static bool
formAnswer(HStoreIterator **it, HStoreValue *v, HEntry *e, bool skipNested)
{
	if (HSE_ISSTRING(*e))
	{
		v->type = hsvString;
		v->string.val = (*it)->data + HSE_OFF(*e);
		v->string.len = HSE_LEN(*e);
		v->size = sizeof(HEntry) + v->string.len;

		return false;
	}
	else if (HSE_ISBOOL(*e))
	{
		v->type = hsvBool;
		v->boolean = (HSE_ISBOOL_TRUE(*e)) ? true : false;
		v->size = sizeof(HEntry);

		return false;
	}
	else if (HSE_ISNUMERIC(*e))
	{
		v->type = hsvNumeric;
		v->numeric = (Numeric)((*it)->data + INTALIGN(HSE_OFF(*e)));
		v->size = 2*sizeof(HEntry) + VARSIZE_ANY(v->numeric);

		return false;
	}
	else if (HSE_ISNULL(*e))
	{
		v->type = hsvNull;
		v->size = sizeof(HEntry);

		return false;
	}
	else if (skipNested)
	{
		v->type = hsvBinary;
		v->binary.data = (*it)->data + INTALIGN(HSE_OFF(*e));
		v->binary.len = HSE_LEN(*e) - (INTALIGN(HSE_OFF(*e)) - HSE_OFF(*e));
		v->size = v->binary.len + 2*sizeof(HEntry);

		return false;
	}
	else
	{
		HStoreIterator *nit = palloc(sizeof(*nit));

		parseBuffer(nit, (*it)->data + INTALIGN(HSE_OFF(*e)));
		nit->next = *it;
		*it = nit;

		return true;
	}
}

static HStoreIterator*
up(HStoreIterator *it)
{
	HStoreIterator *v = it->next;

	pfree(it);

	return v;
}

int
HStoreIteratorGet(HStoreIterator **it, HStoreValue *v, bool skipNested)
{
	int res;

	if (*it == NULL)
		return 0;

	/*
	 * Encode all possible states by one integer. That's possible
	 * because enum members of HStoreIterator->state uses different bits
	 * than HS_FLAG_ARRAY/HS_FLAG_HSTORE. See definition of HStoreIterator
	 */

	switch((*it)->type | (*it)->state)
	{
		case HS_FLAG_ARRAY | hsi_start:
			(*it)->state = hsi_elem;
			(*it)->i = 0;
			v->type = hsvArray;
			v->array.nelems = (*it)->nelems;
			res = WHS_BEGIN_ARRAY;
			v->array.scalar = (*it)->isScalar;
			break;
		case HS_FLAG_ARRAY | hsi_elem:
			if ((*it)->i >= (*it)->nelems)
			{
				*it = up(*it);
				res = WHS_END_ARRAY;
			}
			else if (formAnswer(it, v, &(*it)->array[ (*it)->i++ ], skipNested))
			{
				res = HStoreIteratorGet(it, v, skipNested);
			}
			else
			{
				res = WHS_ELEM;
			}
			break;
		case HS_FLAG_HSTORE | hsi_start:
			(*it)->state = hsi_key;
			(*it)->i = 0;
			v->type = hsvHash;
			v->hash.npairs = (*it)->nelems;
			res = WHS_BEGIN_HASH;
			break;
		case HS_FLAG_HSTORE | hsi_key:
			if ((*it)->i >= (*it)->nelems)
			{
				*it = up(*it);
				res = WHS_END_HASH;
			}
			else
			{
				formAnswer(it, v, &(*it)->array[ (*it)->i * 2 ], false);
				(*it)->state = hsi_value;
				res = WHS_KEY;
			}
			break;
		case HS_FLAG_HSTORE | hsi_value:
			(*it)->state = hsi_key;
			if (formAnswer(it, v, &(*it)->array[ ((*it)->i++) * 2 + 1],
						   skipNested))
				res = HStoreIteratorGet(it, v, skipNested);
			else
				res = WHS_VALUE;
			break;
		default:
			elog(PANIC,"unknown state %08x", (*it)->type & (*it)->state);
	}

	return res;
}

/****************************************************************************
 *        Transformation from tree to binary representation of hstore       *
 ****************************************************************************/
typedef struct CompressState
{
	char	*begin;
	char	*ptr;

	struct {
		uint32	i;
		uint32	*header;
		HEntry	*array;
		char	*begin;
	} *levelstate, *lptr, *pptr;

	uint32	maxlevel;

} CompressState;

#define	curLevelState	state->lptr
#define prevLevelState	state->pptr

static void
putHEntryString(CompressState *state, HStoreValue* value,
				uint32 level, uint32 i)
{
	curLevelState = state->levelstate + level;

	if (i == 0)
		curLevelState->array[0].entry = HENTRY_ISFIRST;
	else
		curLevelState->array[i].entry = 0;

	switch(value->type)
	{
		case hsvNull:
			curLevelState->array[i].entry |= HENTRY_ISNULL;

			if (i>0)
				curLevelState->array[i].entry |=
					curLevelState->array[i - 1].entry & HENTRY_POSMASK;
			break;
		case hsvString:
			memcpy(state->ptr, value->string.val, value->string.len);
			state->ptr += value->string.len;

			if (i == 0)
				curLevelState->array[i].entry |= value->string.len;
			else
				curLevelState->array[i].entry |=
					(curLevelState->array[i - 1].entry & HENTRY_POSMASK) +
					value->string.len;
			break;
		case hsvBool:
			curLevelState->array[i].entry |=
				(value->boolean) ? HENTRY_ISTRUE : HENTRY_ISFALSE;

			if (i>0)
				curLevelState->array[i].entry |=
					curLevelState->array[i - 1].entry & HENTRY_POSMASK;
			break;
		case hsvNumeric:
			{
				int addlen = INTALIGN(state->ptr - state->begin) -
								(state->ptr - state->begin);
				int	numlen = VARSIZE_ANY(value->numeric);

				switch(addlen)
				{
					case 3:
						*state->ptr = '\0'; state->ptr++;
					case 2:
						*state->ptr = '\0'; state->ptr++;
					case 1:
						*state->ptr = '\0'; state->ptr++;
					case 0:
					default:
						break;
				}

				memcpy(state->ptr, value->numeric, numlen);
				state->ptr += numlen;

				curLevelState->array[i].entry |= HENTRY_ISNUMERIC;
				if (i == 0)
					curLevelState->array[i].entry |= addlen + numlen;
				else
					curLevelState->array[i].entry |=
						(curLevelState->array[i - 1].entry & HENTRY_POSMASK) +
						addlen + numlen;
				break;
			}
		case hsvBinary:
			{
				int addlen = INTALIGN(state->ptr - state->begin) -
								(state->ptr - state->begin);

				switch(addlen)
				{
					case 3:
						*state->ptr = '\0'; state->ptr++;
					case 2:
						*state->ptr = '\0'; state->ptr++;
					case 1:
						*state->ptr = '\0'; state->ptr++;
					case 0:
					default:
						break;
				}

				memcpy(state->ptr, value->binary.data, value->binary.len);
				state->ptr += value->binary.len;

				curLevelState->array[i].entry |= HENTRY_ISNEST;

				if (i == 0)
					curLevelState->array[i].entry |= addlen + value->binary.len;
				else
					curLevelState->array[i].entry |=
						(curLevelState->array[i - 1].entry & HENTRY_POSMASK) +
						addlen + value->binary.len;
			}
			break;
		default:
			elog(PANIC,"Unsupported HStoreValue type: %d", value->type);
	}
}

static void
compressCallback(void *arg, HStoreValue* value, uint32 flags, uint32 level)
{
	CompressState	*state = arg;

	if (level == state->maxlevel) {
		state->maxlevel *= 2;
		state->levelstate = repalloc(state->levelstate,
								 sizeof(*state->levelstate) * state->maxlevel);
	}

	curLevelState = state->levelstate + level;

	if (flags & (WHS_BEGIN_ARRAY | WHS_BEGIN_HASH))
	{
		Assert(((flags & WHS_BEGIN_ARRAY) && value->type == hsvArray) ||
			   ((flags & WHS_BEGIN_HASH) && value->type == hsvHash));

		curLevelState->begin = state->ptr;

		switch(INTALIGN(state->ptr - state->begin) -
			   			(state->ptr - state->begin))
		{
			case 3:
				*state->ptr = '\0'; state->ptr++;
			case 2:
				*state->ptr = '\0'; state->ptr++;
			case 1:
				*state->ptr = '\0'; state->ptr++;
			case 0:
			default:
				break;
		}

		curLevelState->header = (uint32*)state->ptr;
		state->ptr += sizeof(*curLevelState->header);

		curLevelState->array = (HEntry*)state->ptr;
		curLevelState->i = 0;

		if (value->type == hsvArray)
		{
			*curLevelState->header = value->array.nelems | HS_FLAG_ARRAY ;
			state->ptr += sizeof(HEntry) * value->array.nelems;

			if (value->array.scalar)
			{
				Assert(value->array.nelems == 1);
				Assert(level == 0);
				*curLevelState->header |= HS_FLAG_SCALAR;
			}
		}
		else
		{
			*curLevelState->header = value->hash.npairs | HS_FLAG_HSTORE ;
			state->ptr += sizeof(HEntry) * value->hash.npairs * 2;
		}

		if (level == 0)
			*curLevelState->header |= HS_FLAG_NEWVERSION;
	}
	else if (flags & WHS_ELEM)
	{
		putHEntryString(state, value, level, curLevelState->i);
		curLevelState->i++;
	}
	else if (flags & WHS_KEY)
	{
		Assert(value->type == hsvString);

		putHEntryString(state, value, level, curLevelState->i * 2);
	}
	else if (flags & WHS_VALUE)
	{
		putHEntryString(state, value, level, curLevelState->i * 2 + 1);
		curLevelState->i++;
	}
	else if (flags & (WHS_END_ARRAY | WHS_END_HASH))
	{
		uint32	len, i;

		Assert(((flags & WHS_END_ARRAY) && value->type == hsvArray) ||
			   ((flags & WHS_END_HASH) && value->type == hsvHash));
		if (level == 0)
			return;

		len = state->ptr - (char*)curLevelState->begin;

		prevLevelState = curLevelState - 1;

		if (*prevLevelState->header & HS_FLAG_ARRAY) {
			i = prevLevelState->i;

			prevLevelState->array[i].entry = HENTRY_ISNEST;

			if (i == 0)
				prevLevelState->array[0].entry |= HENTRY_ISFIRST | len;
			else
				prevLevelState->array[i].entry |=
					(prevLevelState->array[i - 1].entry & HENTRY_POSMASK) + len;
		}
		else if (*prevLevelState->header & HS_FLAG_HSTORE)
		{
			i = 2 * prevLevelState->i + 1; /* VALUE, not a KEY */

			prevLevelState->array[i].entry = HENTRY_ISNEST;

			prevLevelState->array[i].entry |=
				(prevLevelState->array[i - 1].entry & HENTRY_POSMASK) + len;
		}
		else
		{
			elog(PANIC, "Wrong parent");
		}

		Assert(state->ptr - curLevelState->begin <= value->size);
		prevLevelState->i++;
	}
	else
	{
		elog(PANIC, "Wrong flags");
	}
}

uint32
compressHStore(HStoreValue *v, char *buffer) {
	uint32			l = 0;
	CompressState	state;

	state.begin = state.ptr = buffer;
	state.maxlevel = 8;
	state.levelstate = palloc(sizeof(*state.levelstate) * state.maxlevel);

	walkUncompressedHStore(v, compressCallback, &state);

	l = state.ptr - buffer;
	Assert(l <= v->size);

	return l;
}

/****************************************************************************
 *                  Iteration-like forming hstore                           *
 *       Note: it believ by default in already sorted keys in hash,         *
 *     although with r == WHS_END_HASH and v == NULL  it will sort itself   *
 ****************************************************************************/
static ToHStoreState*
pushState(ToHStoreState **state)
{
	ToHStoreState	*ns = palloc(sizeof(*ns));

	ns->next = *state;
	return ns;
}

static void
appendArray(ToHStoreState *state, HStoreValue *v)
{
	HStoreValue	*a = &state->v;

	Assert(a->type == hsvArray);

	if (a->array.nelems >= state->size)
	{
		state->size *= 2;
		a->array.elems = repalloc(a->array.elems,
								   sizeof(*a->array.elems) * state->size);
	}

	a->array.elems[a->array.nelems ++] = *v;

	a->size += v->size;
}

static void
appendKey(ToHStoreState *state, HStoreValue *v)
{
	HStoreValue	*h = &state->v;

	Assert(h->type == hsvHash);

	if (h->hash.npairs >= state->size)
	{
		state->size *= 2;
		h->hash.pairs = repalloc(h->hash.pairs,
									sizeof(*h->hash.pairs) * state->size);
	}

	h->hash.pairs[h->hash.npairs].key = *v;
	h->hash.pairs[h->hash.npairs].order = h->hash.npairs;

	h->size += v->size;
}

static void
appendValue(ToHStoreState *state, HStoreValue *v)
{

	HStoreValue	*h = &state->v;

	Assert(h->type == hsvHash);

	h->hash.pairs[h->hash.npairs++].value = *v;

	h->size += v->size;
}


HStoreValue*
pushHStoreValue(ToHStoreState **state, int r /* WHS_* */, HStoreValue *v) {
	HStoreValue	*h = NULL;

	switch(r)
	{
		case WHS_BEGIN_ARRAY:
			*state = pushState(state);
			h = &(*state)->v;
			(*state)->v.type = hsvArray;
			(*state)->v.size = 3*sizeof(HEntry);
			(*state)->v.array.nelems = 0;
			(*state)->v.array.scalar = (v && v->array.scalar) ? true : false;
			(*state)->size = (v && v->type == hsvArray && v->array.nelems > 0)
								? v->array.nelems : 4;
			(*state)->v.array.elems = palloc(sizeof(*(*state)->v.array.elems) *
											 		(*state)->size);
			break;
		case WHS_BEGIN_HASH:
			*state = pushState(state);
			h = &(*state)->v;
			(*state)->v.type = hsvHash;
			(*state)->v.size = 3*sizeof(HEntry);
			(*state)->v.hash.npairs = 0;
			(*state)->size = (v && v->type == hsvHash && v->hash.npairs > 0)
								? v->hash.npairs : 4;
			(*state)->v.hash.pairs = palloc(sizeof(*(*state)->v.hash.pairs) *
													(*state)->size);
			break;
		case WHS_ELEM:
			Assert(v->type == hsvNull || v->type == hsvString ||
				   v->type == hsvBool || v->type == hsvNumeric ||
				   v->type == hsvBinary);
			appendArray(*state, v);
			break;
		case WHS_KEY:
			Assert(v->type == hsvString);
			appendKey(*state, v);
			break;
		case WHS_VALUE:
			Assert(v->type == hsvNull || v->type == hsvString ||
				   v->type == hsvBool || v->type == hsvNumeric ||
				   v->type == hsvBinary);
			appendValue(*state, v);
			break;
		case WHS_END_HASH:
			h = &(*state)->v;
			if (v == NULL)
				uniqueHStoreValue(h);
		case WHS_END_ARRAY:
			h = &(*state)->v;
			*state = (*state)->next;
			if (*state)
			{
				switch((*state)->v.type)
				{
					case hsvArray:
						appendArray(*state, h);
						break;
					case hsvHash:
						appendValue(*state, h);
						break;
					default:
						elog(PANIC, "wrong parent type: %d", (*state)->v.type);
				}
			}
			break;
		default:
			elog(PANIC, "wrong type: %08x", r);
	}

	return h;
}

