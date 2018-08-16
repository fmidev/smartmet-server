pipeline {
  agent {
    node {
      label 'master'
    }

  }
  stages {
    stage('Build') {
      agent {
        docker {
          image 'fmidev'
          args '-v /tmp/shared-ccache:/ccache -v /tmp/shared-yum-cache:/var/cache/yum -v ${PWD}:/work -w /work'
          reuseNode true
        }

      }
      steps {
        sh '''
git clean -ffxd
rpmlint *.spec
rm -rf /tmp/$JOB_NAME
mkdir -p /tmp/$JOB_NAME
echo %_rpmdir /tmp/$JOB_NAME > $HOME/.rpmmacros
echo %_srcrpmdir /tmp/$JOB_NAME >> $HOME/.rpmmacros
yum-builddep -y *.spec
make rpm
mkdir -p dist/src
mkdir -p dist/bin
find /tmp/$JOB_NAME -name *.src.rpm | xargs -I RPM mv RPM dist/src/
find /tmp/$JOB_NAME -name *.rpm | xargs -I RPM mv RPM dist/bin/
rm -rf /tmp/$JOB_NAME
'''
      }
    }
    stage('Install') {
      agent {
        docker {
          image 'fmibase'
          args '-v /tmp/shared-ccache:/ccache -v /tmp/shared-yum-cache:/var/cache/yum -v ${PWD}:/work -w /work'
          reuseNode true
        }

      }
      steps {
        sh 'ls --recursive -la dist/ ; yum install -y dist/bin/*.rpm ; rpm -qp dist/bin/*.rpm | xargs rpm --query'
      }
    }
    stage('Final') {
      steps {
        sh 'pwd ; ls --recursive -l dist/'
        archiveArtifacts(artifacts: 'dist/**/*.rpm', fingerprint: true, onlyIfSuccessful: true)
      }
    }
  }
}