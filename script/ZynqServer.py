##############################################################################
## This file is part of 'axi-soc-ultra-plus-core'.
## It is subject to the license terms in the LICENSE.txt file found in the
## top-level directory of this distribution and at:
##    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
## No part of 'axi-soc-ultra-plus-core', including this file,
## may be copied, modified, propagated, or distributed except according to
## the terms contained in the LICENSE.txt file.
##############################################################################

import pyrogue
import rogue
import rogue.hardware.axi
import rogue.interfaces.memory

import socket
import ipaddress
import time
import argparse

# rogue.Logging.setLevel(rogue.Logging.Debug)

#################################################################

# Set the argument parser
parser = argparse.ArgumentParser()

# Add arguments
parser.add_argument(
    "--numLane",
    type     = int,
    required = False,
    default  = 1,
    help     = "# of DMA Lanes",
)

parser.add_argument(
    "--numVc",
    type     = int,
    required = False,
    default  = 1,
    help     = "# of VC (virtual channels) per lane",
)

# Get the arguments
args = parser.parse_args()

#################################################################

class ZynqTcpServer(object):

    def __init__(self):

        # Memory server on port 9000
        print( 'Starting Memory TcpServer at Port=9000')
        self.memMap = rogue.hardware.axi.AxiMemMap('/dev/axi_memory_map')
        self.memServer = rogue.interfaces.memory.TcpServer('*', 9000)
        pyrogue.busConnect(self.memServer, self.memMap)

        # Data server on port 10000+512*lane+2*vc
        self.dmaStream = [[None for x in range(args.numVc)] for y in range(args.numLane)]
        self.dataServer = [[None for x in range(args.numVc)] for y in range(args.numLane)]
        for lane in range(args.numLane):
            for vc in range(args.numVc):
                port = 10000+512*lane+2*vc
                print( f'Starting Stream TcpServer at Port={port} and Port={port+1} for DMA[{lane}][{vc}]' )
                self.dmaStream[lane][vc] = rogue.hardware.axi.AxiStreamDma('/dev/axi_stream_dma_0', 256*lane+vc, True)
                self.dataServer[lane][vc] = rogue.interfaces.stream.TcpServer('*',port)
                self.dmaStream[lane][vc] == self.dataServer[lane][vc]

#################################################################

if __name__ == '__main__':

    server = ZynqTcpServer()

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print('Stopping TcpServers')

#################################################################