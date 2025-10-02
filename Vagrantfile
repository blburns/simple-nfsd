# -*- mode: ruby -*-
# vi: set ft=ruby :

# Multi-VM Vagrantfile for Simple NFS Daemon
# Manages VMs in the virtuals/ directory structure

Vagrant.configure("2") do |config|
  # Define VM configurations
  vm_configs = {
    "ubuntu_dev" => {
      box: "ubuntu/jammy64",
      hostname: "simple-nfsd-ubuntu-dev",
      ip: "192.168.1.100",
      memory: "2048",
      cpus: 2
    },
    "centos_dev" => {
      box: "centos/8",
      hostname: "simple-nfsd-centos-dev", 
      ip: "192.168.1.101",
      memory: "2048",
      cpus: 2
    }
  }
  
  # Get the VM type from environment variable or default to ubuntu_dev
  vm_type = ENV['VAGRANT_BOX'] || "ubuntu_dev"
  
  # Validate VM type
  unless vm_configs.key?(vm_type)
    puts "Error: Unknown VM type '#{vm_type}'"
    puts "Available types: #{vm_configs.keys.join(', ')}"
    exit 1
  end
  
  vm_config = vm_configs[vm_type]
  
  # VM configuration
  config.vm.box = vm_config[:box]
  config.vm.hostname = vm_config[:hostname]
  
  # Network configuration
  config.vm.network "private_network", ip: vm_config[:ip]
  
  # VM resources
  config.vm.provider "virtualbox" do |vb|
    vb.name = vm_config[:hostname]
    vb.memory = vm_config[:memory]
    vb.cpus = vm_config[:cpus]
    vb.gui = false  # Use headless mode
    vb.customize ["modifyvm", :id, "--natdnshostresolver1", "on"]
    vb.customize ["modifyvm", :id, "--natdnsproxy1", "on"]
    vb.customize ["modifyvm", :id, "--vrde", "off"]  # Disable VRDE for headless
  end
  
  # Provisioning with Ansible
  config.vm.provision "ansible" do |ansible|
    ansible.playbook = "automation/playbook.yml"
    ansible.inventory_path = "automation/inventory.ini"
    ansible.limit = "development"
    ansible.extra_vars = {
      git_repo_url: ".",
      git_branch: "main"
    }
    ansible.verbose = true
  end
  
  # Shared directories for development
  config.vm.synced_folder ".", "/vagrant", type: "rsync", 
    rsync__exclude: [".git/", "dist/", "*.o", "*.so", "*.a", "virtuals/", ".vagrant/"]
  
  # Application source code (read-write for development)
  config.vm.synced_folder "./src", "/opt/simple-nfsd/src", type: "rsync"
  config.vm.synced_folder "./include", "/opt/simple-nfsd/include", type: "rsync"
  config.vm.synced_folder "./tests", "/opt/simple-nfsd/tests", type: "rsync"
  config.vm.synced_folder "./config", "/opt/simple-nfsd/config", type: "rsync"
  config.vm.synced_folder "./CMakeLists.txt", "/opt/simple-nfsd/CMakeLists.txt", type: "rsync"
  config.vm.synced_folder "./Makefile", "/opt/simple-nfsd/Makefile", type: "rsync"
  
  # Build directory (read-write for build artifacts)
  config.vm.synced_folder "./build", "/opt/simple-nfsd/build", type: "rsync"
  
  # Post-provisioning script
  config.vm.provision "shell", inline: <<-SHELL
    # Create log directories
    sudo mkdir -p /var/log/simple-nfsd
    sudo chown nfsdev:nfsdev /var/log/simple-nfsd
    
    # Create configuration directory
    sudo mkdir -p /etc/simple-nfsd
    sudo chown nfsdev:nfsdev /etc/simple-nfsd
    
    # Enable and start services
    sudo systemctl enable simple-nfsd
    sudo systemctl start simple-nfsd
    
    # Show status
    echo "=== Simple NFS Daemon Status ==="
    sudo systemctl status simple-nfsd --no-pager
    echo ""
    echo "=== Test Results ==="
    cd /opt/simple-nfsd/build && sudo -u nfsdev make test
  SHELL
end
