#!/bin/sh

# Check that all files that should have the current version agree on it

AC_PATH=configure.ac
AC_VERS=$(sed '/AC_INIT/!d; s/AC_INIT(dsme, \(.*\))/\1/' $AC_PATH)

RPM_PATH=${RPM_SOURCE_DIR:-rpm}/${RPM_PACKAGE_NAME:-dsme}.spec
RPM_VERS=$(grep '^Version:' $RPM_PATH |sed -e 's/^.*:[[:space:]]*//')

YAML_PATH=rpm/dsme.yaml
YAML_VERS=$(grep '^Version:' $YAML_PATH |sed -e 's/^.*:[[:space:]]*//')

RES=0

if [ "$RPM_VERS" != "$AC_VERS" ]; then
  echo >&2 "$AC_PATH $AC_VERS vs $RPM_PATH $RPM_VERS"
  RES=1
fi

if [ "$RPM_VERS" != "$YAML_VERS" ]; then
  echo >&2 "$YAML_PATH $YAML_VERS vs $RPM_PATH $RPM_VERS"
  RES=1
fi

if [ $RES != 0 ]; then
  echo >&2 "Conflicting package versions"
fi

exit $RES
