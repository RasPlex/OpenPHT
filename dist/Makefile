VERSION=1.6.2

rpm-prep:
	mkdir -p ${HOME}/rpmbuild/SOURCES/
	tar --transform="s/\./openpht-${VERSION}/" -cf ${HOME}/rpmbuild/SOURCES/openpht-${VERSION}.tar.gz --exclude=./build --exclude=.git --exclude=*.bz ./ --gzip

rpm: rpm-prep
	rpmbuild -ba dist/openpht.spec
