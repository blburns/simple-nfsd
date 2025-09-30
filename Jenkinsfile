#!/usr/bin/env groovy
// Jenkins Pipeline for simple-nfsd
// Simple NFS Daemon - A lightweight and secure NFS server
// Copyright  SimpleDaemons

pipeline {
    agent any
    
    parameters {
        choice(
            name: 'BUILD_TYPE',
            choices: ['Release', 'Debug'],
            description: 'Build type'
        )
        choice(
            name: 'PLATFORM',
            choices: ['linux', 'macos', 'windows'],
            description: 'Target platform'
        )
        booleanParam(
            name: 'RUN_TESTS',
            defaultValue: true,
            description: 'Run tests'
        )
        booleanParam(
            name: 'RUN_ANALYSIS',
            defaultValue: true,
            description: 'Run static analysis'
        )
        booleanParam(
            name: 'CREATE_PACKAGES',
            defaultValue: false,
            description: 'Create distribution packages'
        )
        booleanParam(
            name: 'DEPLOY',
            defaultValue: false,
            description: 'Deploy artifacts'
        )
    }
    
    environment {
        PROJECT_NAME = 'simple-nfsd'
        VERSION = '${env.BUILD_NUMBER}'
        BUILD_DIR = 'build'
        DIST_DIR = 'dist'
        DOCKER_IMAGE = "${PROJECT_NAME}:${VERSION}"
        DOCKER_REGISTRY = 'your-registry.com'
    }
    
    options {
        timeout(time: 60, unit: 'MINUTES')
        timestamps()
        ansiColor('xterm')
        buildDiscarder(logRotator(numToKeepStr: '10'))
        skipDefaultCheckout()
    }
    
    stages {
        stage('Checkout') {
            steps {
                checkout scm
                script {
                    env.GIT_COMMIT_SHORT = sh(
                        script: 'git rev-parse --short HEAD',
                        returnStdout: true
                    ).trim()
                }
            }
        }
        
        stage('Setup') {
            parallel {
                stage('Linux Setup') {
                    when {
                        anyOf {
                            equals expected: 'linux', actual: params.PLATFORM
                            equals expected: 'all', actual: params.PLATFORM
                        }
                    }
                    steps {
                        sh '''
                            sudo apt-get update -qq
                            sudo apt-get install -y -qq build-essential cmake libssl-dev libjsoncpp-dev pkg-config
                            sudo apt-get install -y -qq clang-format cppcheck python3-pip
                            pip3 install --user bandit semgrep
                        '''
                    }
                }
                
                stage('macOS Setup') {
                    when {
                        anyOf {
                            equals expected: 'macos', actual: params.PLATFORM
                            equals expected: 'all', actual: params.PLATFORM
                        }
                    }
                    steps {
                        sh '''
                            brew update
                            brew install cmake openssl jsoncpp clang-format cppcheck
                            pip3 install --user bandit semgrep
                        '''
                    }
                }
                
                stage('Windows Setup') {
                    when {
                        anyOf {
                            equals expected: 'windows', actual: params.PLATFORM
                            equals expected: 'all', actual: params.PLATFORM
                        }
                    }
                    steps {
                        bat '''
                            echo "Windows setup would go here"
                            echo "Install Visual Studio, CMake, vcpkg, etc."
                        '''
                    }
                }
            }
        }
        
        stage('Build') {
            parallel {
                stage('Linux Build') {
                    when {
                        anyOf {
                            equals expected: 'linux', actual: params.PLATFORM
                            equals expected: 'all', actual: params.PLATFORM
                        }
                    }
                    steps {
                        sh '''
                            mkdir -p ${BUILD_DIR}
                            cd ${BUILD_DIR}
                            cmake .. -DCMAKE_BUILD_TYPE=${BUILD_TYPE}
                            make -j$(nproc)
                        '''
                    }
                }
                
                stage('macOS Build') {
                    when {
                        anyOf {
                            equals expected: 'macos', actual: params.PLATFORM
                            equals expected: 'all', actual: params.PLATFORM
                        }
                    }
                    steps {
                        sh '''
                            mkdir -p ${BUILD_DIR}
                            cd ${BUILD_DIR}
                            cmake .. -DCMAKE_BUILD_TYPE=${BUILD_TYPE}
                            make -j$(sysctl -n hw.ncpu)
                        '''
                    }
                }
                
                stage('Windows Build') {
                    when {
                        anyOf {
                            equals expected: 'windows', actual: params.PLATFORM
                            equals expected: 'all', actual: params.PLATFORM
                        }
                    }
                    steps {
                        bat '''
                            mkdir build
                            cd build
                            cmake .. -G "Visual Studio 16 2019" -A x64 -DCMAKE_BUILD_TYPE=%BUILD_TYPE%
                            cmake --build . --config %BUILD_TYPE%
                        '''
                    }
                }
            }
        }
        
        stage('Test') {
            when {
                expression { params.RUN_TESTS == true }
            }
            parallel {
                stage('Linux Test') {
                    when {
                        anyOf {
                            equals expected: 'linux', actual: params.PLATFORM
                            equals expected: 'all', actual: params.PLATFORM
                        }
                    }
                    steps {
                        sh '''
                            cd ${BUILD_DIR}
                            make test
                        '''
                    }
                }
                
                stage('macOS Test') {
                    when {
                        anyOf {
                            equals expected: 'macos', actual: params.PLATFORM
                            equals expected: 'all', actual: params.PLATFORM
                        }
                    }
                    steps {
                        sh '''
                            cd ${BUILD_DIR}
                            make test
                        '''
                    }
                }
                
                stage('Windows Test') {
                    when {
                        anyOf {
                            equals expected: 'windows', actual: params.PLATFORM
                            equals expected: 'all', actual: params.PLATFORM
                        }
                    }
                    steps {
                        bat '''
                            cd build
                            ctest --output-on-failure -C %BUILD_TYPE%
                        '''
                    }
                }
            }
        }
        
        stage('Analysis') {
            when {
                expression { params.RUN_ANALYSIS == true }
            }
            parallel {
                stage('Code Style') {
                    steps {
                        sh '''
                            make check-style
                        '''
                    }
                }
                
                stage('Static Analysis') {
                    steps {
                        sh '''
                            make lint
                        '''
                    }
                }
                
                stage('Security Scan') {
                    steps {
                        sh '''
                            make security-scan
                        '''
                    }
                }
            }
        }
        
        stage('Package') {
            when {
                expression { params.CREATE_PACKAGES == true }
            }
            steps {
                sh '''
                    make package
                    ls -la ${DIST_DIR}/
                '''
            }
        }
        
        stage('Docker') {
            steps {
                script {
                    def dockerfile = 'Dockerfile'
                    if (fileExists(dockerfile)) {
                        sh '''
                            docker build -t ${DOCKER_IMAGE} .
                            docker tag ${DOCKER_IMAGE} ${DOCKER_REGISTRY}/${DOCKER_IMAGE}
                        '''
                    } else {
                        echo "No Dockerfile found, skipping Docker build"
                    }
                }
            }
        }
        
        stage('Deploy') {
            when {
                expression { params.DEPLOY == true }
            }
            steps {
                script {
                    // Deploy to artifact repository
                    sh '''
                        echo "Deploying artifacts..."
                        # Add your deployment logic here
                        # e.g., upload to Nexus, Artifactory, etc.
                    '''
                    
                    // Deploy Docker image
                    if (fileExists('Dockerfile')) {
                        sh '''
                            docker push ${DOCKER_REGISTRY}/${DOCKER_IMAGE}
                        '''
                    }
                }
            }
        }
    }
    
    post {
        always {
            // Archive artifacts
            archiveArtifacts artifacts: 'dist/**', allowEmptyArchive: true
            archiveArtifacts artifacts: 'build/**', allowEmptyArchive: true
            
            // Clean up
            cleanWs()
        }
        
        success {
            echo 'Build succeeded!'
            // Send success notification
            emailext (
                subject: "Build Success: ${env.JOB_NAME} - ${env.BUILD_NUMBER}",
                body: "Build ${env.BUILD_NUMBER} succeeded for ${env.PROJECT_NAME}",
                to: "${env.CHANGE_AUTHOR_EMAIL}"
            )
        }
        
        failure {
            echo 'Build failed!'
            // Send failure notification
            emailext (
                subject: "Build Failed: ${env.JOB_NAME} - ${env.BUILD_NUMBER}",
                body: "Build ${env.BUILD_NUMBER} failed for ${env.PROJECT_NAME}. Check the console output for details.",
                to: "${env.CHANGE_AUTHOR_EMAIL}"
            )
        }
        
        unstable {
            echo 'Build unstable!'
        }
    }
}
