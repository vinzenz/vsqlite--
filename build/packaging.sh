#!/bin/sh

SOURCE="${BASH_SOURCE[0]}"
DIR="$( dirname "$SOURCE" )"
while [ -h "$SOURCE" ]
do 
  SOURCE="$(readlink "$SOURCE")"
  [[ $SOURCE != /* ]] && SOURCE="$DIR/$SOURCE"
  DIR="$( cd -P "$( dirname "$SOURCE"  )" && pwd )"
done
DIR="$( cd -P "$( dirname "$SOURCE" )" && pwd )"

mv libvsqlite++.so libvsqlite++.so.`cat $DIR/../VERSION` 
ln -s libvsqlite++.so.`cat $DIR/../VERSION` libvsqlite++.so.`head -c 1 $DIR/../VERSION` 
ln -s libvsqlite++.so.`cat $DIR/../VERSION` libvsqlite++.so

