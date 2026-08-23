import itertools

import SCons
from fbt.util import GLOB_FILE_EXCLUSION
from SCons.Node.FS import has_glob_magic
from SCons.Script import Flatten


def GlobRecursive(env, pattern, node=".", exclude=()):
    exclude = sorted(set(Flatten(exclude) + GLOB_FILE_EXCLUSION))
    # print(f"Starting glob for {pattern} from {node} (exclude: {exclude})")
    results = []
    if isinstance(node, str):
        node = env.Dir(node)
    # Only initiate actual recursion if special symbols can be found in 'pattern'
    if has_glob_magic(pattern):
        for f in sorted(
            node.glob("*", source=True, exclude=exclude), key=lambda entry: entry.path
        ):
            if isinstance(f, SCons.Node.FS.Dir):
                results += env.GlobRecursive(pattern, f, exclude)
        results += sorted(
            node.glob(
                pattern,
                source=True,
                exclude=exclude,
            ),
            key=lambda entry: entry.path,
        )
    # Otherwise, just assume that file at path exists
    else:
        results.append(node.File(pattern))
    ## Debug
    # print(f"Glob result for {pattern} from {node}: {results}")
    return sorted(results, key=lambda entry: entry.path)


def GatherSources(env, sources_list, node="."):
    sources_list = list(dict.fromkeys(Flatten(sources_list)))
    include_sources = list(filter(lambda x: not x.startswith("!"), sources_list))
    exclude_sources = list(x[1:] for x in sources_list if x.startswith("!"))
    gathered_sources = list(
        itertools.chain.from_iterable(
            env.GlobRecursive(
                source_type,
                node,
                exclude=exclude_sources,
            )
            for source_type in include_sources
        )
    )
    ## Debug
    # print(
    #     f"Gathered sources for {sources_list} from {node}: {list(f.path for f in gathered_sources)}"
    # )
    return sorted(gathered_sources, key=lambda entry: entry.path)


def generate(env):
    env.AddMethod(GlobRecursive)
    env.AddMethod(GatherSources)


def exists(env):
    return True
