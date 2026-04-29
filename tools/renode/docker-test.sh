#!/bin/bash
declare -r HOST_ROOT_DIR=`pwd`
declare -r HOST_TEST_RESULTS_PATH=${HOST_ROOT_DIR}/test_results
declare -r HOST_LOG_PATH=${HOST_TEST_RESULTS_PATH}
declare -r HOST_LOG_FILENAME=${HOST_LOG_PATH}/logs.txt

declare -r DOCKER_IMAGE="${DOCKER_IMAGE:-ghcr.io/wolfssl/wolfboot-ci-renode:v1.5}"
declare -r DOCKER_WORKSPACE=/workspace
declare -r DOCKER_TEST_RESULTS_PATH=/tmp/test_results

mkdir -p ${HOST_LOG_PATH}

docker pull ${DOCKER_IMAGE} >/dev/null 2>&1 || true

exit_code=0
if ! docker run \
  --log-driver=none -a stdout -a stderr \
  --volume ${HOST_ROOT_DIR}:${DOCKER_WORKSPACE} \
  --volume ${HOST_TEST_RESULTS_PATH}:${DOCKER_TEST_RESULTS_PATH} \
  --env SCRIPT=${DOCKER_WORKSPACE}/renode-config.resc \
  --workdir ${DOCKER_WORKSPACE} \
  ${DOCKER_IMAGE} \
  /bin/bash -c 'tools/scripts/renode-test-update.sh "$@" > '"${DOCKER_TEST_RESULTS_PATH}"'/logs.txt 2>&1' -- "$@"
then
  echo "FAILED"
  exit_code=1
fi

echo -e "\n----- LOGS -----\n"
cat ${HOST_LOG_FILENAME}

if [ $exit_code -eq 0 ]
then
  echo "$1: PASS"
else
  echo "$1: FAIL"
fi

# Raise the exit
exit $exit_code
