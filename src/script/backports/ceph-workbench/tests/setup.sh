set -ex
bash tests/teardown.sh
mkdir data
bash tests/setup-redmine.sh
bash tests/setup-gitlab.sh
