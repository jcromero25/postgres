
SELECT 'ff => {a=>12, b=>16}'::hstore;

SELECT 'ff => {a=>12, b=>16}, qq=> 123'::hstore;

SELECT 'aa => {a,aaa}, qq=>{ a=>12, b=>16 , c=> { c1, c2}, d=>{d1=>d1, d2=>d2, d1=>d3} }'::hstore;

SELECT '"aa"=>{a,aaa}, "qq"=>{"a"=>"12", "b"=>"16", "c"=>{c1,c2}, "d"=>{"d1"=>"d1", "d2"=>"d2"}}'::hstore;

SELECT '"aa"=>{a,aaa}, "qq"=>{"a"=>"12", "b"=>"16", "c"=>{c1,c2,{c3},{c4=>4}}, "d"=>{"d1"=>"d1", "d2"=>"d2"}}'::hstore;

SELECT 'ff => {a,aaa}'::hstore;

--test optional outer braces
SELECT	'a=>1'::hstore;
SELECT	'{a=>1}'::hstore;
SELECT	'a,b'::hstore;
SELECT	'{a,b}'::hstore;
SELECT	'a,{b}'::hstore;
SELECT	'{a,{b}}'::hstore;
SELECT	'{a},b'::hstore;
SELECT	'{{a},b}'::hstore;
SELECT	'a,{b},{c}'::hstore;
SELECT	'{a,{b},{c}}'::hstore;
SELECT	'{a},{b},c'::hstore;
SELECT	'{{a},{b},c}'::hstore;
SELECT	'{a},b,{c}'::hstore;
SELECT	'{{a},b,{c}}'::hstore;
SELECT	'a,{b=>1}'::hstore;
SELECT	'{a,{b=>1}}'::hstore;
SELECT	'{a},{b=>1}'::hstore;
SELECT	'{{a},{b=>1}}'::hstore;
SELECT	'{a},{b=>1},{c}'::hstore;
SELECT	'{{a},{b=>1},{c}}'::hstore;
SELECT	'a'::hstore;
SELECT	'{a}'::hstore;
SELECT	''::hstore;
SELECT	'{}'::hstore;

--nested json

SELECT	hstore_to_json('a=>1');
SELECT	hstore_to_json('{a=>1}');
SELECT	hstore_to_json('a,b');
SELECT	hstore_to_json('{a,b}');
SELECT	hstore_to_json('a,{b}');
SELECT	hstore_to_json('{a,{b}}');
SELECT	hstore_to_json('{a},b');
SELECT	hstore_to_json('{{a},b}');
SELECT	hstore_to_json('a,{b},{c}');
SELECT	hstore_to_json('{a,{b},{c}}');
SELECT	hstore_to_json('{a},{b},c');
SELECT	hstore_to_json('{{a},{b},c}');
SELECT	hstore_to_json('{a},b,{c}');
SELECT	hstore_to_json('{{a},b,{c}}');
SELECT	hstore_to_json('a,{b=>1}');
SELECT	hstore_to_json('{a,{b=>1}}');
SELECT	hstore_to_json('{a},{b=>1}');
SELECT	hstore_to_json('{{a},{b=>1}}');
SELECT	hstore_to_json('{a},{b=>1},{c}');
SELECT	hstore_to_json('{{a},{b=>1},{c}}');
SELECT	hstore_to_json('a');
SELECT	hstore_to_json('{a}');
SELECT	hstore_to_json('');
SELECT	hstore_to_json('{}');

SELECT hstore_to_json('"aa"=>{a,aaa}, "qq"=>{"a"=>"12", "b"=>"16", "c"=>{c1,c2,{c3},{c4=>4}}, "d"=>{"d1"=>"d1", "d2"=>"d2"}}'::hstore);

--

SELECT 'ff => {a=>12, b=>16}, qq=> 123, x=>{1,2}, Y=>NULL'::hstore -> 'ff', 
	   'ff => {a=>12, b=>16}, qq=> 123, x=>{1,2}, Y=>NULL'::hstore -> 'qq', 
	   ('ff => {a=>12, b=>16}, qq=> 123, x=>{1,2}, Y=>NULL'::hstore -> 'Y') IS NULL AS t, 
	   'ff => {a=>12, b=>16}, qq=> 123, x=>{1,2}, Y=>NULL'::hstore -> 'x'; 

SELECT '[ a, b, c, d]'::hstore -> 'a';
--

CREATE TABLE testtype (i int, h hstore, a int[]);
INSERT INTO testtype VALUES (1, 'a=>1', '{1,2,3}');

SELECT populate_record(v, 'i=>2'::hstore) FROM testtype v;
SELECT populate_record(v, 'i=>2, a=>{7,8,9}'::hstore) FROM testtype v;
SELECT populate_record(v, 'i=>2, h=>{b=>3}, a=>{7,8,9}'::hstore) FROM testtype v;

--complex delete

SELECT 'b=>{a,c}'::hstore - 'a'::text;
SELECT 'b=>{a,c}, a=>1'::hstore - 'a'::text;
SELECT 'b=>{a,c}, a=>[2,3]'::hstore - 'a'::text;
SELECT 'b=>{a,c}, a=>[2,3]'::hstore - 'a'::text;
SELECT '[2,3,a]'::hstore - 'a'::text;
SELECT '[a,2,3,a]'::hstore - 'a'::text;
SELECT '[a,a]'::hstore - 'a'::text;
SELECT '[a]'::hstore - 'a'::text;
SELECT 'a=>1'::hstore - 'a'::text;
SELECT ''::hstore - 'a'::text;

SELECT 'a, 1 , b,2, c,3'::hstore - ARRAY['d','b'];

SELECT 'a=>{1,2}, v=>23, b=>c'::hstore - 'v'::hstore;
SELECT 'a=>{1,2}, v=>23, b=>c'::hstore - 'v=>23'::hstore;
SELECT 'a=>{1,2}, v=>23, b=>c'::hstore - 'v=>{1,2}'::hstore;
SELECT 'a=>{1,2}, v=>23, b=>c'::hstore - 'a=>{1,2}'::hstore;
SELECT 'a, {1,2}, v, 23, b, c'::hstore - 'v'::hstore;
SELECT 'a, {1,2}, v, 23, b, c'::hstore - 'v=>23'::hstore;
SELECT 'a, {1,2}, v, 23, b, c'::hstore - 'v,23'::hstore;
SELECT 'a, {1,2}, v, 23, b, c'::hstore - 'v,{1,2}'::hstore;

--joining

SELECT 'aa=>1 , b=>2, cq=>3'::hstore || 'cq,l, b,g, fg,f, 1,2'::hstore;
SELECT 'aa,1 , b,2, cq,3'::hstore || 'cq,l, b,g, fg,f, 1,2'::hstore;

--slice
SELECT slice_array(hstore 'aa=>1, b=>2, c=>3', ARRAY['g','h','i']);
SELECT slice_array(hstore 'aa,1, b,2, c,3', ARRAY['g','h','i']);
SELECT slice_array(hstore 'aa=>1, b=>2, c=>3', ARRAY['b','c']);
SELECT slice_array(hstore 'aa,1, b,2, c,3', ARRAY['b','c']);
SELECT slice_array(hstore 'aa=>1, b=>{2=>1}, c=>{1,2}', ARRAY['b','c']);

SELECT slice(hstore 'aa=>1, b=>2, c=>3', ARRAY['g','h','i']);
SELECT slice(hstore 'aa,1, b,2, c,3', ARRAY['g','h','i']);
SELECT slice(hstore 'aa=>1, b=>2, c=>3', ARRAY['b','c']);
SELECT slice(hstore 'aa,1, b,2, c,3', ARRAY['b','c']);
SELECT slice(hstore 'aa=>1, b=>{2=>1}, c=>{1,2}', ARRAY['b','c']);

--to array
SELECT %% 'aa=>1, cq=>l, b=>{a,n}, fg=>NULL';
SELECT %% 'aa,1, cq,l, b,g, fg,NULL';
SELECT hstore_to_matrix( 'aa=>1, cq=>l, b=>{a,n}, fg=>NULL');
SELECT hstore_to_matrix( 'aa,1, cq,l, b,g, fg,NULL');


--contains
SELECT 'a=>b'::hstore @> 'a=>b, c=>b';
SELECT 'a=>b, c=>b'::hstore @> 'a=>b';
SELECT 'a=>{1,2}, c=>b'::hstore @> 'a=>{1,2}';
SELECT 'a=>{2,1}, c=>b'::hstore @> 'a=>{1,2}';
SELECT 'a=>{1=>2}, c=>b'::hstore @> 'a=>{1,2}';
SELECT 'a=>{2=>1}, c=>b'::hstore @> 'a=>{1,2}';
SELECT 'a=>{1=>2}, c=>b'::hstore @> 'a=>{1=>2}';
SELECT 'a=>{2=>1}, c=>b'::hstore @> 'a=>{1=>2}';
SELECT 'a,b'::hstore @> 'a,b, c,b';
SELECT 'a,b, c,b'::hstore @> 'a,b';
SELECT 'a,b, c,{1,2}'::hstore @> 'a,{1,2}';
SELECT 'a,b, c,{1,2}'::hstore @> 'b,{1,2}';

-- %>

SELECT 'n=>NULL, a=>1, b=>{1,2}, c=>{1=>2}, d=>{1=>{2,3}}'::hstore %> 'n';
SELECT 'n=>NULL, a=>1, b=>{1,2}, c=>{1=>2}, d=>{1=>{2,3}}'::hstore %> 'a';
SELECT 'n=>NULL, a=>1, b=>{1,2}, c=>{1=>2}, d=>{1=>{2,3}}'::hstore %> 'b';
SELECT 'n=>NULL, a=>1, b=>{1,2}, c=>{1=>2}, d=>{1=>{2,3}}'::hstore %> 'c';
SELECT 'n=>NULL, a=>1, b=>{1,2}, c=>{1=>2}, d=>{1=>{2,3}}'::hstore %> 'd';
SELECT 'n=>NULL, a=>1, b=>{1,2}, c=>{1=>2}, d=>{1=>{2,3}}'::hstore %> 'd' %> '1';

SELECT '1,2,3,{a,b}'::hstore %> '1';

SELECT 'n=>NULL, a=>1, b=>{1,2}, c=>{1=>2}, d=>{1=>{2,3}}'::hstore %> 5;
SELECT 'n=>NULL, a=>1, b=>{1,2}, c=>{1=>2}, d=>{1=>{2,3}}'::hstore %> 4;
SELECT 'n=>NULL, a=>1, b=>{1,2}, c=>{1=>2}, d=>{1=>{2,3}}'::hstore %> 3;
SELECT 'n=>NULL, a=>1, b=>{1,2}, c=>{1=>2}, d=>{1=>{2,3}}'::hstore %> 2;
SELECT 'n=>NULL, a=>1, b=>{1,2}, c=>{1=>2}, d=>{1=>{2,3}}'::hstore %> 1;
SELECT 'n=>NULL, a=>1, b=>{1,2}, c=>{1=>2}, d=>{1=>{2,3}}'::hstore %> 0;

SELECT 'a,b, c,{1,2}, NULL'::hstore %> 5;
SELECT 'a,b, c,{1,2}, NULL'::hstore %> 4;
SELECT 'a,b, c,{1,2}, NULL'::hstore %> 3;
SELECT 'a,b, c,{1,2}, NULL'::hstore %> 2;
SELECT 'a,b, c,{1,2}, NULL'::hstore %> 1;
SELECT 'a,b, c,{1,2}, NULL'::hstore %> 0;

-- ->
SELECT 'n=>NULL, a=>1, b=>{1,2}, c=>{1=>2}, d=>{1=>{2,3}}'::hstore -> 5;
SELECT 'n=>NULL, a=>1, b=>{1,2}, c=>{1=>2}, d=>{1=>{2,3}}'::hstore -> 4;
SELECT 'n=>NULL, a=>1, b=>{1,2}, c=>{1=>2}, d=>{1=>{2,3}}'::hstore -> 3;
SELECT 'n=>NULL, a=>1, b=>{1,2}, c=>{1=>2}, d=>{1=>{2,3}}'::hstore -> 2;
SELECT 'n=>NULL, a=>1, b=>{1,2}, c=>{1=>2}, d=>{1=>{2,3}}'::hstore -> 1;
SELECT 'n=>NULL, a=>1, b=>{1,2}, c=>{1=>2}, d=>{1=>{2,3}}'::hstore -> 0;

SELECT 'a,b, c,{1,2}, NULL'::hstore -> 5;
SELECT 'a,b, c,{1,2}, NULL'::hstore -> 4;
SELECT 'a,b, c,{1,2}, NULL'::hstore -> 3;
SELECT 'a,b, c,{1,2}, NULL'::hstore -> 2;
SELECT 'a,b, c,{1,2}, NULL'::hstore -> 1;
SELECT 'a,b, c,{1,2}, NULL'::hstore -> 0;

SELECT 'n=>NULL, a=>1, b=>{1,2}, c=>{1=>2}, d=>{1=>{2,3}}'::hstore -> -6;
SELECT 'n=>NULL, a=>1, b=>{1,2}, c=>{1=>2}, d=>{1=>{2,3}}'::hstore -> -5;
SELECT 'n=>NULL, a=>1, b=>{1,2}, c=>{1=>2}, d=>{1=>{2,3}}'::hstore -> -4;
SELECT 'n=>NULL, a=>1, b=>{1,2}, c=>{1=>2}, d=>{1=>{2,3}}'::hstore -> -3;
SELECT 'n=>NULL, a=>1, b=>{1,2}, c=>{1=>2}, d=>{1=>{2,3}}'::hstore -> -2;
SELECT 'n=>NULL, a=>1, b=>{1,2}, c=>{1=>2}, d=>{1=>{2,3}}'::hstore -> -1;

SELECT 'a,b, c,{1,2}, NULL'::hstore -> -6;
SELECT 'a,b, c,{1,2}, NULL'::hstore -> -5;
SELECT 'a,b, c,{1,2}, NULL'::hstore -> -4;
SELECT 'a,b, c,{1,2}, NULL'::hstore -> -3;
SELECT 'a,b, c,{1,2}, NULL'::hstore -> -2;
SELECT 'a,b, c,{1,2}, NULL'::hstore -> -1;

-- #>

SELECT 'a=>b, c=>{1,2,3}'::hstore #> '{0}';
SELECT 'a=>b, c=>{1,2,3}'::hstore #> '{a}';
SELECT 'a=>b, c=>{1,2,3}'::hstore #> '{c}';
SELECT 'a=>b, c=>{1,2,3}'::hstore #> '{c, 0}';
SELECT 'a=>b, c=>{1,2,3}'::hstore #> '{c, 1}';
SELECT 'a=>b, c=>{1,2,3}'::hstore #> '{c, 2}';
SELECT 'a=>b, c=>{1,2,3}'::hstore #> '{c, 3}';
SELECT 'a=>b, c=>{1,2,3}'::hstore #> '{c, -1}';
SELECT 'a=>b, c=>{1,2,3}'::hstore #> '{c, -2}';
SELECT 'a=>b, c=>{1,2,3}'::hstore #> '{c, -3}';
SELECT 'a=>b, c=>{1,2,3}'::hstore #> '{c, -4}';

SELECT '0, 1, 2, {3,4}, {5=>five}'::hstore #> '{0}';
SELECT '0, 1, 2, {3,4}, {5=>five}'::hstore #> '{3}';
SELECT '0, 1, 2, {3,4}, {5=>five}'::hstore #> '{4}';
SELECT '0, 1, 2, {3,4}, {5=>five}'::hstore #> '{4,5}';

-- #%>

SELECT 'a=>b, c=>{1,2,3}'::hstore #%> '{0}';
SELECT 'a=>b, c=>{1,2,3}'::hstore #%> '{a}';
SELECT 'a=>b, c=>{1,2,3}'::hstore #%> '{c}';
SELECT 'a=>b, c=>{1,2,3}'::hstore #%> '{c, 0}';
SELECT 'a=>b, c=>{1,2,3}'::hstore #%> '{c, 1}';
SELECT 'a=>b, c=>{1,2,3}'::hstore #%> '{c, 2}';
SELECT 'a=>b, c=>{1,2,3}'::hstore #%> '{c, 3}';
SELECT 'a=>b, c=>{1,2,3}'::hstore #%> '{c, -1}';
SELECT 'a=>b, c=>{1,2,3}'::hstore #%> '{c, -2}';
SELECT 'a=>b, c=>{1,2,3}'::hstore #%> '{c, -3}';
SELECT 'a=>b, c=>{1,2,3}'::hstore #%> '{c, -4}';

SELECT '0, 1, 2, {3,4}, {5=>five}'::hstore #%> '{0}';
SELECT '0, 1, 2, {3,4}, {5=>five}'::hstore #%> '{3}';
SELECT '0, 1, 2, {3,4}, {5=>five}'::hstore #%> '{4}';
SELECT '0, 1, 2, {3,4}, {5=>five}'::hstore #%> '{4,5}';

-- ?

SELECT 'n=>NULL, a=>1, b=>{1,2}, c=>{1=>2}, d=>{1=>{2,3}}'::hstore ? 5;
SELECT 'n=>NULL, a=>1, b=>{1,2}, c=>{1=>2}, d=>{1=>{2,3}}'::hstore ? 4;
SELECT 'n=>NULL, a=>1, b=>{1,2}, c=>{1=>2}, d=>{1=>{2,3}}'::hstore ? 3;
SELECT 'n=>NULL, a=>1, b=>{1,2}, c=>{1=>2}, d=>{1=>{2,3}}'::hstore ? 2;
SELECT 'n=>NULL, a=>1, b=>{1,2}, c=>{1=>2}, d=>{1=>{2,3}}'::hstore ? 1;
SELECT 'n=>NULL, a=>1, b=>{1,2}, c=>{1=>2}, d=>{1=>{2,3}}'::hstore ? 0;

SELECT 'a,b, c,{1,2}, NULL'::hstore ? 5;
SELECT 'a,b, c,{1,2}, NULL'::hstore ? 4;
SELECT 'a,b, c,{1,2}, NULL'::hstore ? 3;
SELECT 'a,b, c,{1,2}, NULL'::hstore ? 2;
SELECT 'a,b, c,{1,2}, NULL'::hstore ? 1;
SELECT 'a,b, c,{1,2}, NULL'::hstore ? 0;

SELECT 'n=>NULL, a=>1, b=>{1,2}, c=>{1=>2}, d=>{1=>{2,3}}'::hstore ? -6;
SELECT 'n=>NULL, a=>1, b=>{1,2}, c=>{1=>2}, d=>{1=>{2,3}}'::hstore ? -5;
SELECT 'n=>NULL, a=>1, b=>{1,2}, c=>{1=>2}, d=>{1=>{2,3}}'::hstore ? -4;
SELECT 'n=>NULL, a=>1, b=>{1,2}, c=>{1=>2}, d=>{1=>{2,3}}'::hstore ? -3;
SELECT 'n=>NULL, a=>1, b=>{1,2}, c=>{1=>2}, d=>{1=>{2,3}}'::hstore ? -2;
SELECT 'n=>NULL, a=>1, b=>{1,2}, c=>{1=>2}, d=>{1=>{2,3}}'::hstore ? -1;

SELECT 'a,b, c,{1,2}, NULL'::hstore ? -6;
SELECT 'a,b, c,{1,2}, NULL'::hstore ? -5;
SELECT 'a,b, c,{1,2}, NULL'::hstore ? -4;
SELECT 'a,b, c,{1,2}, NULL'::hstore ? -3;
SELECT 'a,b, c,{1,2}, NULL'::hstore ? -2;
SELECT 'a,b, c,{1,2}, NULL'::hstore ? -1;

SELECT 'a=>b, c=>{1,2,3}'::hstore ? '{0}'::text[];
SELECT 'a=>b, c=>{1,2,3}'::hstore ? '{a}'::text[];
SELECT 'a=>b, c=>{1,2,3}'::hstore ? '{c}'::text[];
SELECT 'a=>b, c=>{1,2,3}'::hstore ? '{b}'::text[];
SELECT 'a=>b, c=>{1,2,3}'::hstore ? '{c, 0}'::text[];
SELECT 'a=>b, c=>{1,2,3}'::hstore ? '{c, 1}'::text[];
SELECT 'a=>b, c=>{1,2,3}'::hstore ? '{c, 2}'::text[];
SELECT 'a=>b, c=>{1,2,3}'::hstore ? '{c, 3}'::text[];
SELECT 'a=>b, c=>{1,2,3}'::hstore ? '{c, -1}'::text[];
SELECT 'a=>b, c=>{1,2,3}'::hstore ? '{c, -2}'::text[];
SELECT 'a=>b, c=>{1,2,3}'::hstore ? '{c, -3}'::text[];
SELECT 'a=>b, c=>{1,2,3}'::hstore ? '{c, -4}'::text[];
SELECT 'a=>b, c=>{1,2,3}'::hstore ? '{c, -5}'::text[];

SELECT '0, 1, 2, {3,4}, {5=>five}'::hstore ? '{0}'::text[];
SELECT '0, 1, 2, {3,4}, {5=>five}'::hstore ? '{3}'::text[];
SELECT '0, 1, 2, {3,4}, {5=>five}'::hstore ? '{4}'::text[];
SELECT '0, 1, 2, {3,4}, {5=>five}'::hstore ? '{4,5}'::text[];

--cast 

SELECT ('{"f2":{"f3":1},"f4":{"f5":99,"f6":"stringy"}}'::text)::hstore AS err;
SELECT ('{"f2":{"f3":1},"f4":{"f5":99,"f6":"stringy"}}'::json)::hstore AS ok;

--decoration

SET hstore.array_brackets=false;
SET hstore.root_array_decorated=false;
SET hstore.root_hash_decorated=false;
SELECT 'a=>1, b=>{c=>3}, d=>[4,[5]]'::hstore AS h, 'a, {b=>c}, [c, d, e]'::hstore AS a;

SET hstore.array_brackets=false;
SET hstore.root_array_decorated=false;
SET hstore.root_hash_decorated=true;
SELECT 'a=>1, b=>{c=>3}, d=>[4,[5]]'::hstore AS h, 'a, {b=>c}, [c, d, e]'::hstore AS a;

SET hstore.array_brackets=false;
SET hstore.root_array_decorated=true;
SET hstore.root_hash_decorated=false;
SELECT 'a=>1, b=>{c=>3}, d=>[4,[5]]'::hstore AS h, 'a, {b=>c}, [c, d, e]'::hstore AS a;

SET hstore.array_brackets=false;
SET hstore.root_array_decorated=true;
SET hstore.root_hash_decorated=true;
SELECT 'a=>1, b=>{c=>3}, d=>[4,[5]]'::hstore AS h, 'a, {b=>c}, [c, d, e]'::hstore AS a;

SET hstore.array_brackets=true;
SET hstore.root_array_decorated=false;
SET hstore.root_hash_decorated=false;
SELECT 'a=>1, b=>{c=>3}, d=>[4,[5]]'::hstore AS h, 'a, {b=>c}, [c, d, e]'::hstore AS a;

SET hstore.array_brackets=true;
SET hstore.root_array_decorated=false;
SET hstore.root_hash_decorated=true;
SELECT 'a=>1, b=>{c=>3}, d=>[4,[5]]'::hstore AS h, 'a, {b=>c}, [c, d, e]'::hstore AS a;

SET hstore.array_brackets=true;
SET hstore.root_array_decorated=true;
SET hstore.root_hash_decorated=false;
SELECT 'a=>1, b=>{c=>3}, d=>[4,[5]]'::hstore AS h, 'a, {b=>c}, [c, d, e]'::hstore AS a;

SET hstore.array_brackets=true;
SET hstore.root_array_decorated=true;
SET hstore.root_hash_decorated=true;
SELECT 'a=>1, b=>{c=>3}, d=>[4,[5]]'::hstore AS h, 'a, {b=>c}, [c, d, e]'::hstore AS a;

RESET hstore.array_brackets;
RESET hstore.root_array_decorated;
RESET hstore.root_hash_decorated;
