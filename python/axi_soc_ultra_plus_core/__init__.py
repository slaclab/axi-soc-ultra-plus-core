from axi_soc_ultra_plus_core._AxiVersion     import *
from axi_soc_ultra_plus_core._SysMonLvAuxDet import *
from axi_soc_ultra_plus_core._AxiSocCore     import *

import click
import subprocess

def pingCheck(ip):
    try:
        subprocess.check_output(["ping", "-c", "1", ip])
    except subprocess.CalledProcessError:
        errMsg = f'Failied to ping {ip}'
        click.secho(errMsg, bg='red')
        raise ValueError(errMsg)
