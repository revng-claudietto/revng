#
# This file is distributed under the MIT License. See LICENSE.md for details.
#

"""
Definitions shared by all the commands of the revng command-line.
"""

from __future__ import annotations

import os
import signal
import sys
from abc import ABC, abstractmethod
from subprocess import Popen
from typing import IO, Any, Callable, Concatenate, Dict, List, Mapping, NoReturn, Optional
from typing import ParamSpec, Tuple, TypeVar, Union

import click

from revng.internal.support.elf import is_executable
from revng.pypeline.cli.context import ClickContext as PypelineClickContext
from revng.pypeline.cli.wrappers import WrappableCommand
from revng.pypeline.utils.logger import Logger
from revng.support import get_command

from .support import relative, search_prefixes

__all__ = [
    "ClickContext",
    "CommandRegistry",
    "WrappableCommand",
    "cli_logger",
    "pass_context",
]

OptionalEnv = Optional[Mapping[str, str]]
# What `Popen` accepts as `stdin`/`stdout`/`stderr`
Redirect = Union[int, IO[Any], None]


class ClickContext(PypelineClickContext):
    """
    The click context used by the revng command-line. It extends the pypeline
    one with the helpers to run the external programs shipped with revng.
    """

    @property
    def verbose(self) -> bool:
        """Whether the user asked for verbose output via `--verbose`."""
        return self.obj.verbose

    def try_run(
        self,
        command,
        environment: OptionalEnv = None,
        stdin: Redirect = None,
        stdout: Redirect = None,
        stderr: Redirect = None,
    ) -> int:
        try:
            signal.signal(signal.SIGINT, signal.SIG_IGN)
            command, environment = self._run_common(command, environment)
            process = Popen(
                command,
                env=environment,
                preexec_fn=lambda: signal.signal(signal.SIGINT, signal.SIG_DFL),
                close_fds=False,
                stdin=stdin,
                stdout=stdout,
                stderr=stderr,
            )
            return process.wait()
        finally:
            signal.signal(signal.SIGINT, signal.SIG_DFL)

    def run(
        self,
        command,
        environment: OptionalEnv = None,
        stdin: Redirect = None,
        stdout: Redirect = None,
        stderr: Redirect = None,
    ) -> int:
        result = self.try_run(command, environment, stdin, stdout, stderr)
        if result != 0:
            sys.exit(result)

        return result

    def exec_run(self, command, environment: OptionalEnv = None) -> NoReturn:
        command, environment = self._run_common(command, environment)
        os.execvpe(command[0], command, environment)

    def _run_common(
        self, command, environment: OptionalEnv = None
    ) -> Tuple[List[str], Dict[str, str]]:
        if not os.path.isfile(command[0]):
            command = [get_command(command[0], search_prefixes()), *command[1:]]

        wrapper = self.obj.wrapper
        if wrapper is not None:
            if is_executable(command[0]):
                command = [*wrapper.prefix, *command]
            else:
                sh = get_command("sh", search_prefixes())
                command = [*wrapper.prefix, sh, "-c", 'exec "$0" "$@"', *command]

        if self.verbose:
            program_path = relative(command[0])
            sys.stderr.write("{}\n\n".format(" \\\n  ".join([program_path] + command[1:])))

        environment = dict(os.environ if environment is None else environment)

        if "valgrind" in command:
            environment["PYTHONMALLOC"] = "malloc"

        return command, environment


class CommandRegistry(ABC):
    """
    Interface used by the `setup` function of each command module to add its
    commands to the revng command-line.
    """

    @abstractmethod
    def register(self, group: tuple[str, ...], command: click.Command):
        """
        Add `command` to the group addressed by `group`, the root command being
        addressed by the empty tuple. Registering into a group that does not
        exist yet is allowed: the command will be added as soon as the group is.
        """


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
