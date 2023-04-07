
from setuptools import setup

from git import Repo

repo = Repo()

# Get version before adding version file
rawVer = repo.git.describe('--tags')

fields = rawVer.split('-')

if len(fields) == 1:
    pyVer = fields[0]
else:
    pyVer = fields[0] + '.dev' + fields[1]

# append version constant to package init
with open('python/axi_soc_ultra_plus_core/__init__.py','a') as vf:
    vf.write(f'\n__version__="{pyVer}"\n')

setup (
   name='axi_soc_ultra_plus_core',
   version=pyVer,
   packages=['axi_soc_ultra_plus_core',
             'axi_soc_ultra_plus_core/hardware',
             'axi_soc_ultra_plus_core/hardware/RealDigitalRfSoc4x2',
             'axi_soc_ultra_plus_core/hardware/XilinxXcu208',
             'axi_soc_ultra_plus_core/hardware/XilinxXcu216',
             'axi_soc_ultra_plus_core/rfsoc_utility',
             'axi_soc_ultra_plus_core/rfsoc_utility/gui',
             'axi_soc_ultra_plus_core/rfsoc_utility/pydm',],
   package_dir={'':'python'},
)

