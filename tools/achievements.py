"""Load and validate SFIII achievement definitions for the recomp runtime.

The runtime's generate() accepts an ``achievements`` list and a game ``title``;
this module turns a JSON definition file into that list without embedding any
game data. Definitions reference work-RAM symbols that must be reviewed per
revision, so loading only validates shape, never semantics.

Schema (rcheevos): [{"id", "title", "description", "points", "trigger"}].
``condition`` is accepted as an alias for ``trigger``; ``points`` defaults to 0.
"""
import json
from pathlib import Path


REQUIRED = ('id', 'title', 'description')


def load(path):
    """Return a validated list of achievement dicts from a JSON file."""
    data = json.loads(Path(path).read_text())
    if not isinstance(data, list):
        raise ValueError('Achievement definition must be a JSON list')
    seen = set()
    result = []
    for index, entry in enumerate(data):
        if not isinstance(entry, dict):
            raise ValueError('Achievement %d must be an object' % index)
        missing = [key for key in REQUIRED if key not in entry]
        if missing:
            raise ValueError('Achievement %d missing %s' % (index, ', '.join(missing)))
        if 'trigger' not in entry and 'condition' not in entry:
            raise ValueError('Achievement %d missing trigger' % index)
        aid = entry['id']
        if not isinstance(aid, (int, str)) or isinstance(aid, bool) or aid == '' or (isinstance(aid, int) and aid < 0):
            raise ValueError('Achievement %d has an invalid id' % index)
        if aid in seen:
            raise ValueError('Duplicate achievement id %s' % aid)
        seen.add(aid)
        for key in ('title', 'description'):
            if not isinstance(entry[key], str) or not entry[key]:
                raise ValueError('Achievement %d has an empty %s' % (index, key))
        trigger = entry.get('trigger', entry.get('condition'))
        if not isinstance(trigger, str) or not trigger:
            raise ValueError('Achievement %d has an empty trigger' % index)
        points = entry.get('points', 0)
        if type(points) is not int or points < 0:
            raise ValueError('Achievement %d has invalid points' % index)
        result.append(dict(id=aid, title=entry['title'], description=entry['description'],
                           points=points, trigger=trigger))
    return result