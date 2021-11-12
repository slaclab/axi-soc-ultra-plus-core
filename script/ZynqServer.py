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
import rogue.hardware.axi
import rogue.interfaces.memory

import socket
import ipaddress
import time

class ZynqTcpServer(object):

    def __init__(self):

        print('Starting TcpServer')

        self.memMap = rogue.hardware.axi.AxiMemMap('/dev/axi_memory_map')

        # Memory server on port 9000        
        self.memServer = rogue.interfaces.memory.TcpServer('*', 9000)
        pyrogue.busConnect(self.memServer, self.memMap)

if __name__ == '__main__':

    server = ZynqTcpServer()

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print('Stopping TcpServer')