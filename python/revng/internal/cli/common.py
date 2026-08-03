#
# This file is distributed under the MIT License. See LICENSE.md for details.
#

"""
Definitions shared by all the commands of the revng command-line.
"""

from typing import Callable, Concatenate, ParamSpec, TypeVar

import click

from revng.pypeline.cli.context import ClickContext as PypelineClickContext
from revng.pypeline.cli.wrappers import WrappableCommand
from revng.pypeline.utils.logger import Logger

__all__ = ["ClickContext", "WrappableCommand", "cli_logger", "pass_context"]


class ClickContext(PypelineClickContext):
    """
    The click context used by the revng command-line. It extends the pypeline
    one with the helpers needed by the commands that are specific to revng.
    """

    @property
    def verbose(self) -> bool:
        """Whether the user asked for verbose output via `--verbose`."""
        return self.obj.verbose


class _CliLogger(Logger):
    """
    A `Logger` whose debug output is enabled by the global `--verbose` option,
    i.e. by the `verbose` property of the current `ClickContext`. It can also be
    enabled explicitly, which is handy for the modules that double as
    standalone scripts.
    """

    @property
    def debug(self) -> bool:
        if self._forced_debug:
            return True
        ctx = click.get_current_context(silent=True)
        return isinstance(ctx, ClickContext) and ctx.obj is not None and ctx.verbose

    @debug.setter
    def debug(self, value: bool) -> None:
        self._forced_debug = value


cli_logger = _CliLogger("cli")
"""
The pre-initialized logger to use inside the revng command-line code.
"""


P = ParamSpec("P")
R = TypeVar("R")


def pass_context(f: Callable[Concatenate[ClickContext, P], R]) -> Callable[P, R]:
    return click.pass_context(f)  # type: ignore
