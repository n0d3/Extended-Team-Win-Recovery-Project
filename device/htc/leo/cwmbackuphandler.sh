#!/sbin/sh

cd $1

split -l1 nandroid.md5

for f in x*
do
	ptnName=`cat $f | cut -d ' ' -f 3 | cut -d '.' -f 1`
	# If ptnName is empty then it is probably android_secure's case which starts with a period(.) 
	if [ -z "$ptnName" ]; then
		ptnName=`cat $f | cut -d ' ' -f 3 | cut -d '.' -f 2`
		ptnFStype=`cat $f | cut -d ' ' -f 3 | cut -d '.' -f 3`
		ptnBackupExt=`cat $f | cut -d ' ' -f 3 | cut -d '.' -f 4`
	else
		ptnFStype=`cat $f | cut -d ' ' -f 3 | cut -d '.' -f 2`
		ptnBackupExt=`cat $f | cut -d ' ' -f 3 | cut -d '.' -f 3`
	fi
	newMd5File=""
	if [ -z "$ptnBackupExt" ]; then
		newMd5File=$ptnName.$ptnFStype.md5
	else
		newMd5File=$ptnName.$ptnFStype.$ptnBackupExt.md5
	fi
	
	if [ -z "$newMd5File" ]; then
		rm -f $f
	else
		mv $f $newMd5File
	fi
done
rm -f nandroid.md5
