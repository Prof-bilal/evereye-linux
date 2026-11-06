"""Deterministic local release archives; no downloads or installation."""
from contextlib import contextmanager
import gzip
import os
import tarfile


@contextmanager
def reproducible_archive(path):
    epoch = int(os.environ.get('SOURCE_DATE_EPOCH', '0'))
    if not 0 <= epoch <= 0xffffffff:
        raise ValueError('SOURCE_DATE_EPOCH must fit an unsigned 32-bit gzip timestamp')

    def normalize(info):
        info.uid = info.gid = 0
        info.uname = info.gname = 'root'
        info.mtime = epoch
        # Preserve executability, without recording the builder's umask.
        info.mode = 0o755 if info.isdir() or info.mode & 0o111 else 0o644
        info.pax_headers = {}
        return info

    with open(path, 'wb') as output:
        with gzip.GzipFile(filename='', mode='wb', fileobj=output, mtime=epoch) as compressed:
            with tarfile.open(fileobj=compressed, mode='w', format=tarfile.PAX_FORMAT) as archive:
                yield archive, normalize
