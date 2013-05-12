# MySQL full-text parser plugin - mecab

## What's this?

It's mysqlftppc fork repository as mysqlftppc-mecab-1.6.1.  
It has get compatible with MySQL-5.5 and MySQL-5.6

## Usage

It is a example to install mysqlftppc for CentOS-6.x.

#### install mecab

```
$ sudo yum localinstall http://packages.groonga.org/centos/6/x86_64/Packages/mecab-0.996-1.el6.x86_64.rpm \
  http://packages.groonga.org/centos/6/x86_64/Packages/mecab-devel-0.996-1.el6.x86_64.rpm \
  http://packages.groonga.org/centos/6/x86_64/Packages/mecab-ipadic-2.7.0.20070801-6.el6.1.x86_64.rpm
```

#### edit my.cnf

First, add configuration to `[mysqld]` section. and then, restart mysql.  

```
# mysqlftppc
mecab_normalization = KC
mecab_unicode_version = 3.2
mecab_dicdir = /usr/lib64/mecab/dic/ipadic
```

Note:  
You can get `mecab_dicdir` path with command `mecab --dump-config | grep dicdir`.

#### install mysqlftppc-mecab

install dependency libraries.

```sh
$ sudo yum install libicu-devel
$ sudo yum localinstall http://packages.groonga.org/centos/6/x86_64/Packages/mecab-0.996-1.el6.x86_64.rpm \
  http://packages.groonga.org/centos/6/x86_64/Packages/mecab-devel-0.996-1.el6.x86_64.rpm \
  http://packages.groonga.org/centos/6/x86_64/Packages/mecab-ipadic-2.7.0.20070801-6.el6.1.x86_64.rpm
```
build mysqlftppc-mecab and install it.

```sh
$ git clone https://github.com/y-ken/mysqlftppc-mecab.git
$ cd mysqlftppc-mecab
$ aclocal && libtoolize --automake && automake --add-missing && automake && autoconf
$ ./configure --with-mysql-config=`which mysql_config` --with-mecab-config=`which mecab-config` --with-icu-config=`which icu-config`
$ make
$ sudo make install
$ mysql -uroot -p -e "install plugin mecab soname 'libftmecab.so';"
```

#### working check

```sql
mysql> CREATE TABLE ft_mecab (c TEXT, FULLTEXT(c) WITH PARSER mecab) ENGINE=MyISAM DEFAULT CHARSET=utf8;
mysql> INSERT INTO ft_mecab VALUES("今日の天気は晴れです。");
mysql> SELECT * FROM ft_mecab WHERE MATCH(c) AGAINST('+"天気"' IN BOOLEAN MODE);
```
## Blog Articles

全文検索パーサプラグイン「mysqlftppc」のMySQL-5.6対応版を作りました  
http://y-ken.hatenablog.com/entry/mysql-parser-plugin-mysqlftppc
