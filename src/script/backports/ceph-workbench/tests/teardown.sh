for i in test-gitlab gitlab-postgresql test-redis ; do sudo docker stop $i || true ; sudo docker rm $i || true ; done
for i in  redmine postgresql-redmine ; do sudo docker stop $i || true ; sudo docker rm $i || true ; done
sudo rm -fr data
