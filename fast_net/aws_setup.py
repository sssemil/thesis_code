import subprocess
import boto3
import json
import os
import time

AMI_ID_X86 = "ami-04a81a99f5ec58529"
AMI_ID_ARM = "ami-0c14ff330901e49ff"
INSTANCE_TYPE = "c7gn.16xlarge"
AMI_ID = AMI_ID_ARM

KEY_NAME = "main"
SSH_KEY_PATH = "~/.ssh/id_rsa"
USER = "ubuntu"

ec2 = boto3.client(
    'ec2',
    region_name='us-east-1'
)

RESOURCE_FILE = 'aws_resources.json'

def save_resources(resources):
    with open(RESOURCE_FILE, 'w') as f:
        json.dump(resources, f)
    print(f"Saved resource IDs to {RESOURCE_FILE}")

def load_resources():
    if os.path.exists(RESOURCE_FILE):
        with open(RESOURCE_FILE, 'r') as f:
            resources = json.load(f)
        print(f"Loaded resource IDs from {RESOURCE_FILE}")
        return resources
    else:
        return {}

def create_vpc():
    response = ec2.create_vpc(CidrBlock='10.0.0.0/16')
    vpc_id = response['Vpc']['VpcId']
    print(f"Created VPC with ID: {vpc_id}")

    ec2.modify_vpc_attribute(VpcId=vpc_id, EnableDnsSupport={'Value': True})
    ec2.modify_vpc_attribute(VpcId=vpc_id, EnableDnsHostnames={'Value': True})

    return vpc_id

def create_internet_gateway(vpc_id):
    response = ec2.create_internet_gateway()
    igw_id = response['InternetGateway']['InternetGatewayId']
    print(f"Created Internet Gateway with ID: {igw_id}")

    ec2.attach_internet_gateway(InternetGatewayId=igw_id, VpcId=vpc_id)
    return igw_id

def create_route_table(vpc_id, igw_id):
    response = ec2.create_route_table(VpcId=vpc_id)
    route_table_id = response['RouteTable']['RouteTableId']
    print(f"Created Route Table with ID: {route_table_id}")

    ec2.create_route(
        RouteTableId=route_table_id,
        DestinationCidrBlock='0.0.0.0/0',
        GatewayId=igw_id
    )
    return route_table_id

def create_subnet(vpc_id):
    response = ec2.create_subnet(
        VpcId=vpc_id,
        CidrBlock='10.0.1.0/24',
        AvailabilityZone='us-east-1f'
    )
    subnet_id = response['Subnet']['SubnetId']
    print(f"Created Subnet with ID: {subnet_id}")

    ec2.modify_subnet_attribute(SubnetId=subnet_id, MapPublicIpOnLaunch={'Value': True})

    return subnet_id

def associate_route_table(subnet_id, route_table_id):
    ec2.associate_route_table(SubnetId=subnet_id, RouteTableId=route_table_id)
    print(f"Associated Route Table {route_table_id} with Subnet {subnet_id}")

def create_security_group(vpc_id):
    response = ec2.create_security_group(
        GroupName='all_traffic_sg',
        Description='Allow all TCP and UDP traffic between hosts',
        VpcId=vpc_id
    )
    sg_id = response['GroupId']
    print(f"Created Security Group with ID: {sg_id}")

    ec2.authorize_security_group_ingress(
        GroupId=sg_id,
        IpPermissions=[
            {
                'IpProtocol': 'tcp',
                'FromPort': 0,
                'ToPort': 65535,
                'UserIdGroupPairs': [{'GroupId': sg_id}]
            },
            {
                'IpProtocol': 'udp',
                'FromPort': 0,
                'ToPort': 65535,
                'UserIdGroupPairs': [{'GroupId': sg_id}]
            },
            {
                'IpProtocol': 'tcp',
                'FromPort': 22,
                'ToPort': 22,
                'IpRanges': [{'CidrIp': '0.0.0.0/0'}]
            }
        ]
    )

    return sg_id

def list_instances():
    instances = ec2.describe_instances()
    running_instances = []
    stopped_instances = []
    for reservation in instances['Reservations']:
        for instance in reservation['Instances']:
            state = instance['State']['Name']
            public_ip = get_public_ip(instance['InstanceId'])
            instance['PublicIpAddress'] = public_ip
            if state == 'running':
                running_instances.append(instance)
            elif state == 'stopped':
                stopped_instances.append(instance)
    return running_instances, stopped_instances

def print_instances(instances):
    for idx, instance in enumerate(instances):
        instance_id = instance['InstanceId']
        instance_type = instance['InstanceType']
        launch_time = instance['LaunchTime']
        state = instance['State']['Name']
        print(
            f"{idx + 1}. ID: {instance_id}, Type: {instance_type}, Launch Time: {launch_time}, State: {state}, Public IP: {instance['PublicIpAddress']}"
        )

def choose_instance(instances):
    print_instances(instances)
    choice = int(input("Enter the number of the instance you want to choose: ")) - 1
    if choice < 0 or choice >= len(instances):
        print("Invalid choice.")
        return None
    return instances[choice]

def launch_instance(subnet_id, sg_id):
    response = ec2.run_instances(
        ImageId=AMI_ID,
        InstanceType=INSTANCE_TYPE,
        KeyName=KEY_NAME,
        MinCount=2,
        MaxCount=2,
        NetworkInterfaces=[
            {
                'DeviceIndex': 0,
                'SubnetId': subnet_id,
                'AssociatePublicIpAddress': True,
                'Groups': [sg_id],
                'DeleteOnTermination': True
            }
        ]
    )
    instance_id = response['Instances'][0]['InstanceId']
    return instance_id

def wait_for_instance_running(instance_id):
    ec2.get_waiter('instance_running').wait(InstanceIds=[instance_id])

def allocate_and_associate_eip(instance_id):
    response = ec2.allocate_address(Domain='vpc')
    allocation_id = response['AllocationId']
    print(f"Allocation ID: {allocation_id}")

    primary_network_interface_id = ec2.describe_instances(InstanceIds=[instance_id])['Reservations'][0]['Instances'][0]['NetworkInterfaces'][0]['NetworkInterfaceId']
    ec2.associate_address(AllocationId=allocation_id, NetworkInterfaceId=primary_network_interface_id)

    public_ip = ec2.describe_addresses(AllocationIds=[allocation_id])['Addresses'][0]['PublicIp']
    return public_ip

def get_public_ip(instance_id):
    reservations = ec2.describe_instances(InstanceIds=[instance_id])['Reservations']
    if reservations:
        instances = reservations[0]['Instances']
        if instances and 'NetworkInterfaces' in instances[0]:
            network_interfaces = instances[0]['NetworkInterfaces']
            if network_interfaces and 'Association' in network_interfaces[0] and 'PublicIp' in network_interfaces[0]['Association']:
                return network_interfaces[0]['Association']['PublicIp']
    return None

def terminate_instance_and_release_ip(instance_id):
    ec2.terminate_instances(InstanceIds=[instance_id])
    ec2.get_waiter('instance_terminated').wait(InstanceIds=[instance_id])
    print(f"Instance {instance_id} terminated.")

    addresses = ec2.describe_addresses()
    for address in addresses['Addresses']:
        if 'AssociationId' in address:
            ec2.disassociate_address(AssociationId=address['AssociationId'])
        ec2.release_address(AllocationId=address['AllocationId'])
    print(f"Released associated Elastic IPs for instance {instance_id}.")

def terminate_all_instances_and_release_ips():
    instances = ec2.describe_instances(
        Filters=[{'Name': 'instance-state-name', 'Values': ['running', 'stopped']}]
    )

    instance_ids = []
    for reservation in instances['Reservations']:
        for instance in reservation['Instances']:
            instance_ids.append(instance['InstanceId'])

    if instance_ids:
        print(f"Terminating instances: {instance_ids}")
        ec2.terminate_instances(InstanceIds=instance_ids)

    addresses = ec2.describe_addresses()

    for address in addresses['Addresses']:
        if 'AssociationId' in address:
            print(f"Disassociating Elastic IP: {address['PublicIp']}")
            ec2.disassociate_address(AssociationId=address['AssociationId'])
        print(f"Releasing Elastic IP: {address['PublicIp']}")
        ec2.release_address(AllocationId=address['AllocationId'])

def run_ssh_command(public_ip, command):
    ssh_command = f"ssh -o \"StrictHostKeyChecking no\" -i {SSH_KEY_PATH} {USER}@{public_ip} '{command}'"
    subprocess.run(ssh_command, shell=True)

def run_setup_script(public_ip):
    subprocess.run(
        f"scp -o \"StrictHostKeyChecking no\" -i {SSH_KEY_PATH} aws_setup.sh {USER}@{public_ip}:",
        shell=True
    )
    run_ssh_command(public_ip, 'chmod +x aws_setup.sh')
    run_ssh_command(public_ip, './aws_setup.sh')

def stop_instance(instance_id):
    ec2.stop_instances(InstanceIds=[instance_id])
    ec2.get_waiter('instance_stopped').wait(InstanceIds=[instance_id])
    print(f"Instance {instance_id} stopped.")

def start_instance(instance_id):
    ec2.start_instances(InstanceIds=[instance_id])
    ec2.get_waiter('instance_running').wait(InstanceIds=[instance_id])
    print(f"Instance {instance_id} started.")

def main():
    resources = load_resources()

    if not resources:
        vpc_id = create_vpc()
        igw_id = create_internet_gateway(vpc_id)
        route_table_id = create_route_table(vpc_id, igw_id)
        subnet_id = create_subnet(vpc_id)
        associate_route_table(subnet_id, route_table_id)
        sg_id = create_security_group(vpc_id)

        resources = {
            'vpc_id': vpc_id,
            'igw_id': igw_id,
            'route_table_id': route_table_id,
            'subnet_id': subnet_id,
            'sg_id': sg_id
        }
        save_resources(resources)
    else:
        vpc_id = resources['vpc_id']
        igw_id = resources['igw_id']
        route_table_id = resources['route_table_id']
        subnet_id = resources['subnet_id']
        sg_id = resources['sg_id']
        print("Using existing AWS resources.")

    while True:
        running_instances, stopped_instances = list_instances()
        print("\nRunning Instances:")
        print_instances(running_instances)
        print("\nStopped Instances:")
        print_instances(stopped_instances)

        print("\nOptions:")
        print("1. Enter a running instance")
        print("2. Resume a stopped instance")
        print("3. Launch a new instance")
        print("4. Terminate an instance and release its IP")
        print("5. Terminate all instances and release IPs")
        print("6. Exit")
        choice = int(input("Choose an option: "))
        if choice == 1:
            if not running_instances:
                print("No running instances available.")
            else:
                instance = choose_instance(running_instances)
                if instance:
                    public_ip = instance['PublicIpAddress']
                    ssh_command = f"ssh -o \"StrictHostKeyChecking no\" -i {SSH_KEY_PATH} {USER}@{public_ip}"
                    subprocess.run(ssh_command, shell=True)
        elif choice == 2:
            if not stopped_instances:
                print("No stopped instances available.")
            else:
                instance = choose_instance(stopped_instances)
                if instance:
                    instance_id = instance['InstanceId']
                    start_instance(instance_id)
                    public_ip = get_public_ip(instance_id)
                    print(f"Public IP: {public_ip}")
                    run_setup_script(public_ip)
                    ssh_command = f"ssh -o \"StrictHostKeyChecking no\" -i {SSH_KEY_PATH} {USER}@{public_ip}"
                    subprocess.run(ssh_command, shell=True)
        elif choice == 3:
            instance_id = launch_instance(subnet_id, sg_id)
            print(f"Instance ID: {instance_id}")
            wait_for_instance_running(instance_id)
            print(f"Instance {instance_id} is running.")
            public_ip = allocate_and_associate_eip(instance_id)
            print(f"Public IP: {public_ip}")
            run_setup_script(public_ip)
            ssh_command = f"ssh -o \"StrictHostKeyChecking no\" -i {SSH_KEY_PATH} {USER}@{public_ip}"
            subprocess.run(ssh_command, shell=True)
        elif choice == 4:
            instances = running_instances + stopped_instances
            if not instances:
                print("No instances available to terminate.")
            else:
                instance = choose_instance(instances)
                if instance:
                    instance_id = instance['InstanceId']
                    terminate_instance_and_release_ip(instance_id)
        elif choice == 5:
            terminate_all_instances_and_release_ips()
        elif choice == 6:
            print("Exiting script.")
            break
        else:
            print("Invalid choice.")

if __name__ == "__main__":
    main()
