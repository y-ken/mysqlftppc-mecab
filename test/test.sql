SET GLOBAL mecab_normalization=OFF;
SET GLOBAL mecab_unicode_version="DEFAULT";

DROP TABLE IF EXISTS me;
CREATE TABLE me (a TEXT, FULLTEXT(a) WITH PARSER mecab) CHARSET utf8;
INSERT INTO me VALUES ("今日は晴天です。");
INSERT INTO me VALUES ("dummy");
INSERT INTO me VALUES ("dummy");
SELECT COUNT(*) FROM me WHERE MATCH(a) AGAINST('今日は晴天');
SELECT COUNT(*) FROM me WHERE MATCH(a) AGAINST('+今日' IN BOOLEAN MODE);
SELECT COUNT(*) FROM me WHERE MATCH(a) AGAINST('+\"今日は晴天\"' IN BOOLEAN MODE);
DROP TABLE IF EXISTS me;

SET GLOBAL mecab_normalization=OFF;
SET GLOBAL mecab_unicode_version="DEFAULT";
