import argparse
import os
import sys
import glob
from workload_spec import *
import multiprocessing
import subprocess


parser = argparse.ArgumentParser(
                prog='Apply SimPoint',
                description='Cluster BBVs')

parser.add_argument('--interval',
                type=int,
                default=10**7,
                help='Set simpoints interval duration in instructions')

parser.add_argument('--workdir',
                type=str,
                default="/home/michal/Downloads/spec_2017_rate_checkpoints",
                help='Path to input/output directory')

parser.add_argument('--simpointbin',
                type=str,
                default="/home/michal/Desktop/Simpoint3.2/bin/simpoint",
                help='Path to simpoints binary file)')

parser.add_argument('--maxcluster',
                type=int,
                default=30,
                help='Maximum value of K to be accepted for search')

parser.add_argument('-j', '--jobs',
                type=int,
                default=os.cpu_count(),
                help='Number of jobs to run in parallel')

parser.add_argument('--clean',
                action='store_true',
                default=False,
                help='Clean existing ')

args = parser.parse_args()

#Ensure absolute paths
args.simpointbin = os.path.abspath(args.simpointbin)
args.workdir = os.path.abspath(args.workdir)

#Delete simpoints if requested
if args.clean:
    os.system(f'/bin/bash -c "shopt -s globstar; rm {args.workdir}/**/simpoints.simpts"')
    os.system(f'/bin/bash -c "shopt -s globstar; rm {args.workdir}/**/simpoints.weights"')
    os.system(f'/bin/bash -c "shopt -s globstar; rm {args.workdir}/**/simpoints.log"')
    os.system(f'/bin/bash -c "shopt -s globstar; rm {args.workdir}/**/simpoints.done"')
    sys.exit()

#Construct list of commands to be executed in parallel
commands = []
bbvpaths = glob.glob(args.workdir + "/**/bb.done", recursive=True)
bbvpaths.sort()

for bbvpath in bbvpaths:
    bbvdir ='/'.join(bbvpath.split('/')[:-1])

    #Skip generating clusters for this item if already successfully generated
    if os.path.exists(bbvdir + '/simpoints.done'):
        with open(bbvdir + '/simpoints.done', 'r') as exitcode:
            if int(exitcode.read().strip()) == 0:
                print(f'Skipped {bbvdir} (simpoints.done exists with 0 exit code)')
                continue

    #Skip generating clusters if bb generation failed
    with open(bbvdir + '/bb.done', 'r') as exitcode:
        if int(exitcode.read().strip()) != 0:
            print(f'Skipped {bbvdir} (bb.done with non-zero exit code)')
            continue
    
    command = []
    command.extend([args.simpointbin, 
                    '-maxK', f'{args.maxcluster}',
                    '-numInitSeeds', '1', 
                    '-loadFVFile', f'{bbvdir}/bb.txt',
                    '-saveSimpoints', f'{bbvdir}/simpoints.simpts',
                    '-saveSimpointWeights', f'{bbvdir}/simpoints.weights'])
    
    commands.append((bbvdir, command))

#Function for single blocking program call
def run_command(command_tuple):
    bbvdir, command = command_tuple

    print(f"Running {bbvdir}")

    with open(bbvdir + "/simpoints.log", 'w+') as log:
        log.write(' '.join(command))

        process = subprocess.Popen(command, stdout=log, stderr=log)
        (output, err) = process.communicate()  
        p_status = process.wait()

    with open(bbvdir + "/simpoints.done", 'w+') as statusf:
        statusf.write(str(p_status))
    
    print(f"Finished {bbvdir} with exit code {p_status}")
        
#Execute commands in parallel
pool = multiprocessing.Pool(args.jobs)

with pool:
    pool.map(run_command, commands)

print("Done")