#
# This file is distributed under the MIT License. See LICENSE.md for details.
#

"""
This is just a wrapper over `pype` that sets pipebox to the revng pipebox path.
The path is computed relatively to this file, so this should work regardless of
where revng is installed.
"""

import os
import signal
import sys
from collections import defaultdict
from importlib import import_module
from inspect import isfunction
from pathlib import Path

import click

from revng.internal.support import cache_directory
from revng.internal.support.collect import collect_files_recursive
from revng.pypeline.cli.project import project
from revng.pypeline.main import pype, run

from .common import ClickContext, CommandRegistry, WrappableCommand, pass_context
from .pypeline_commands import init, quick, run_analysis_native, run_pipe_native
from .support import is_file_executable, search_prefixes

# Groups that do not implement anything on their own, they only exist to
# namespace the commands they contain
NAMESPACES: list[tuple[tuple[str, ...], str, str]] = [
    ((), "model", "Model manipulation helpers"),
    (("model",), "import", "Model import helpers"),
    (("model",), "export", "Model export helpers"),
    ((), "internal", "Internal CLI tools for testing purposes"),
]


class GroupRegistry(CommandRegistry):
    """
    Registry of the click groups making up the revng command-line, addressed by
    their path, e.g. `("model", "import")` for `revng model import`; the root
    command is addressed by the empty tuple.
    """

    def __init__(self, root: click.Group):
        self.groups: dict[tuple[str, ...], click.Group] = {}
        # Commands whose group has not been registered yet
        self.pending: dict[tuple[str, ...], list[click.Command]] = defaultdict(list)
        self._add_group((), root)

    def register(self, group: tuple[str, ...], command: click.Command):
        path = tuple(group)
        if path not in self.groups:
            self.pending[path].append(command)
            return

        self.groups[path].add_command(command)
        if isinstance(command, click.Group):
            assert command.name is not None
            self._add_group((*path, command.name), command)

    def check(self):
        assert not self.pending, "Commands registered in non-existing group(s): " + ", ".join(
            " ".join(group) for group in self.pending
        )

    def _add_group(self, path: tuple[str, ...], group: click.Group):
        assert path not in self.groups
        self.groups[path] = group

        # Groups that have been populated without going through the registry
        # (e.g. the pypeline ones) still need to be addressable
        for name, command in group.commands.items():
            if isinstance(command, click.Group):
                self._add_group((*path, name), command)

        for command in self.pending.pop(path, []):
            self.register(path, command)


def patch_pype():
    """
    revng is based on `pype`, but we want to change some defaults to be revng specific,
    and we want to add some commands.
    """
    # Make click build our own context, which provides revng-specific helpers
    click.Command.context_class = ClickContext

    # Replace the name (needed for autocompletion and usage)
    pype.name = "revng"
    # Replace the default for pipebox
    for param in pype.params:
        if param.name == "pipebox":
            param.default = Path(__file__).parent.parent / "pipebox.py"

    # Change the default for pipeline
    for param in project.params:
        if param.name == "pipeline":
            param.default = Path(__file__).parent.parent / "pipeline.yml"
        elif param.name == "cache_dir":
            param.default = str(cache_directory())
        elif param.name == "storage_provider":
            param.envvar = ["REVNG_STORAGE_PROVIDER", param.envvar]


def build_external_command(group: tuple[str, ...], name: str, path: str) -> click.Command:
    """Build the command forwarding its arguments to an external executable."""
    command_line = " ".join((os.path.basename(sys.argv[0]), *group, name))

    @click.command(
        cls=WrappableCommand,
        name=name,
        help=f"see {command_line} --help",
        add_help_option=False,
        context_settings={"ignore_unknown_options": True, "allow_extra_args": True},
    )
    @click.argument("arguments", metavar="[ARGS]...", nargs=-1, type=click.UNPROCESSED)
    @pass_context
    def external_command(ctx: ClickContext, arguments: tuple[str, ...]) -> int:
        return ctx.try_run([path, *arguments])

    return external_command


def discover_external_commands(registry: GroupRegistry):
    """
    Register the executables in `libexec/revng` as commands. Their name is
    split on `-` to find the innermost group they belong to, e.g. `model-opt`
    becomes `model opt`.
    """
    for executable, path in collect_files_recursive(search_prefixes(), ["libexec", "revng"], "*"):
        if not (is_file_executable(path) and os.path.splitext(path)[1] == ""):
            continue

        group, name = resolve_command_path(registry, executable)
        if name in registry.groups[group].commands:
            continue

        registry.register(group, build_external_command(group, name, path))


def resolve_command_path(registry: GroupRegistry, executable: str) -> tuple[tuple[str, ...], str]:
    """
    Turn the path of an executable, relative to `libexec/revng`, into the group
    it belongs to and its command name. Each directory is a group, then the
    longest prefix of `-`-separated words matching a group is consumed.
    """
    name = executable
    group: tuple[str, ...] = ()
    if "/" in executable:
        path_parts = os.path.split(executable)
        name = path_parts[-1]
        group = tuple(path_parts[:-1])
        # The directories might not be groups yet
        for index in range(1, len(group) + 1):
            if group[:index] not in registry.groups:
                registry.register(group[: index - 1], click.Group(group[index - 1]))

    parts = name.split("-")
    total = len(parts)

    start_index = 0
    found = True
    while found and start_index < total:
        found = False
        for end_index in range(start_index + 1, total + 1):
            candidate = "-".join(parts[start_index:end_index])
            new_group = (*group, candidate)
            if new_group in registry.groups:
                group = new_group
                start_index = end_index
                found = True
                break

    return group, "-".join(parts[start_index:])


def load_commands(registry: CommandRegistry):
    """Let each module in `_commands` register the commands it implements."""
    modules = []
    with os.scandir(Path(__file__).parent / "_commands") as scan:
        for entry in scan:
            entry_path = Path(entry.path)
            if entry_path.name.startswith("__") or entry_path.name.startswith("."):
                continue
            if entry.is_file():
                modules.append(import_module(f"._commands.{entry_path.stem}", __package__))
            elif entry.is_dir():
                modules.append(import_module(f"._commands.{entry_path.name}", __package__))

    for module in modules:
        setup = getattr(module, "setup", None)
        if setup is not None and isfunction(setup):
            setup(registry)


def build_registry() -> GroupRegistry:
    """Create the registry and populate it with the revng-specific commands."""
    registry = GroupRegistry(pype)

    for parent, name, help_text in NAMESPACES:
        registry.register(parent, click.Group(name, help=help_text))

    registry.register((), quick)
    # Add `init` to project subcommand
    registry.register(("project",), init)
    # Add native counterparts to the pipeline subcommand
    registry.register(("pipeline",), run_pipe_native)
    registry.register(("pipeline",), run_analysis_native)

    load_commands(registry)
    discover_external_commands(registry)

    return registry


def main():
    """Entry point for revng."""
    signal.signal(signal.SIGINT, lambda x, y: sys.exit(1))
    patch_pype()
    registry = build_registry()
    registry.check()
    run()


if __name__ == "__main__":
    main()
