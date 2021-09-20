set -ex
DATA=$(pwd)/data
mkdir -p $DATA
sudo docker run --name=test-redis -d sameersbn/redis:latest
sudo rm -fr $DATA/gitlab-postgresql
mkdir -p $DATA/gitlab-postgresql
sudo docker run --name gitlab-postgresql -d --env 'DB_NAME=gitlabhq_production' --env 'DB_USER=gitlab' --env "DB_PASS=Wrobyak4" --env 'DB_EXTENSION=pg_trgm' --volume $DATA/gitlab-postgresql:/var/lib/postgresql sameersbn/postgresql:9.5-3
sudo rm -fr $DATA/gitlab
mkdir -p $DATA/gitlab
key=4W44tm7bJFRPWNMVzKngffxVWXRpVs49dxhFwgpx7FbCj3wXCMmsz47LzWsdr7nM
# do *NOT* set GITLAB_ROOT_PASSWORD=admin because it will silently fail (sanity check probably, because it's too simple or reserved)
sudo docker run --name='test-gitlab' -it -d --link gitlab-postgresql:postgresql --link test-redis:redisio -e DEBUG=true -e 'GITLAB_SIGNUP=true' -e 'GITLAB_PORT=80' -e 'GITLAB_HOST=localhost' -e 'GITLAB_SSH_PORT=8022' -p 8022:22 -p 8181:80 -e GITLAB_SECRETS_SECRET_KEY_BASE=$key -e GITLAB_SECRETS_DB_KEY_BASE=$key -e GITLAB_SECRETS_OTP_KEY_BASE=$key -e GITLAB_ROOT_PASSWORD='5iveL!fe' -v /var/run/docker.sock:/run/docker.sock -v $DATA/gitlab/data:/home/git/data -v $(which docker):/bin/docker sameersbn/gitlab:8.16.3
success=false
for delay in 15 15 15 15 15 30 30 30 30 30 30 30 30 60 60 60 60 120 240 512 ; do
    sleep $delay
    if wget -O - http://127.0.0.1:8181/users/sign_in | grep -q 'About GitLab' ; then
        success=true
        break
    fi
done
$success
