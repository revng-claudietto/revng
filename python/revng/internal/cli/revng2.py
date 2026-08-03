#
# This file is distributed under the MIT License. See LICENSE.md for details.
#

"""
This is just a wrapper over `pype` that sets pipebox to the revng pipebox path.
The path is computed relatively to this file, so this should work regardless of
where revng is installed.
"""

import signal
import sys
from pathlib import Path

import click

from revng.internal.support import cache_directory
from revng.pypeline.cli.pipeline import pipeline
from revng.pypeline.cli.project import project
from revng.pypeline.main import pype, run

from .common import ClickContext
from .pypeline_commands import init, quick, run_analysis_native, run_pipe_native


def patch_pype():
    """
    revng2 is based on `pype`, but we want to change some defaults to be revng specific,
    and we want to add some commands.
    """
    # Make click build our own context, which provides revng-specific helpers
    click.Command.context_class = ClickContext

    # Replace the name (needed for autocompletion and usage)
    pype.name = "revng2"
    pype.add_command(quick)
    # Replace the default for pipebox
    for param in pype.params:
        if param.name == "pipebox":
            param.default = Path(__file__).parent.parent / "pipebox.py"

    # Add `init` to project subcommand
    project.add_command(init)
    # Change the default for pipeline
    for param in project.params:
        if param.name == "pipeline":
            param.default = Path(__file__).parent.parent / "pipeline.yml"
        elif param.name == "cache_dir":
            param.default = str(cache_directory())
        elif param.name == "storage_provider":
            param.envvar = ["REVNG_STORAGE_PROVIDER", param.envvar]

    # Add native counterparts to the pipeline subcommand
    pipeline.add_command(run_pipe_native)
    pipeline.add_command(run_analysis_native)


def main():
    """Entry point for revng2."""
    signal.signal(signal.SIGINT, lambda x, y: sys.exit(1))
    patch_pype()
    run()


if __name__ == "__main__":
    main()
