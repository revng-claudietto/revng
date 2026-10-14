#
# This file is distributed under the MIT License. See LICENSE.md for details.
#

from revng.model.migrations import MigrationBase


class Migration(MigrationBase):
    def migrate(self, binary):
        for function in binary.get("Functions", []):
            if "CallSitePrototypes" in function:
                function["CallSites"] = function.pop("CallSitePrototypes")
