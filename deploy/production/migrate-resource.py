#!/usr/bin/env python3
"""Migrate the existing single-host Compose installation; Python standard library only."""
import argparse
import copy
import configparser
import contextlib
import datetime
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request


class MigrationError(RuntimeError):
    pass


def run(args, *, cwd=None, input=None):
    result = subprocess.run([str(a) for a in args], cwd=cwd, input=input,
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        raise MigrationError(f"Command failed: {args[0]} {args[1]}\n{result.stderr[-1800:]}")
    return result.stdout


def write_private(path, content):
    path = Path(path)
    temporary = path.with_name(path.name + '.migration-tmp')
    with temporary.open('x', encoding='utf-8', newline='\n') as stream:
        os.chmod(temporary, 0o600)
        stream.write(content)
    os.replace(temporary, path)


def nginx_with_resource(text, hostname):
    # Mask comments/quoted strings while retaining positions for brace matching.
    masked = list(text)
    quote = None
    comment = False
    escaped = False
    for i, char in enumerate(text):
        if comment:
            if char == '\n':
                comment = False
            else:
                masked[i] = ' '
        elif quote:
            masked[i] = ' '
            if escaped:
                escaped = False
            elif char == '\\':
                escaped = True
            elif char == quote:
                quote = None
        elif char == '#':
            masked[i] = ' '
            comment = True
        elif char in ('"', "'"):
            masked[i] = ' '
            quote = char
    masked = ''.join(masked)
    matches = []
    for start in re.finditer(r'\bserver\s*\{', masked):
        depth = 1
        end = start.end()
        while depth and end < len(masked):
            depth += (masked[end] == '{') - (masked[end] == '}')
            end += 1
        if depth:
            raise MigrationError('Unbalanced Nginx server block.')
        block = masked[start.end():end - 1]
        names = re.findall(r'\bserver_name\s+([^;]+);', block)
        listens = re.findall(r'\blisten\s+([^;]+);', block)
        if any(hostname in nameset.split() for nameset in names) and any(
                'ssl' in listen.split() and re.search(r'(?:^|:)443(?:\s|$)', listen)
                for listen in listens):
            matches.append((start.end(), end - 1))
    if len(matches) != 1:
        raise MigrationError('Expected exactly one HTTPS server block for the resource URL hostname; use --nginx-site with its actual file.')
    begin, end = matches[0]
    if '/api/resources/v1' in text[begin:end]:
        raise MigrationError('Resource route already exists. Refusing to add or overwrite it automatically.')
    snippet = '''
    # Resource migration: keep the existing API and chat entry points.
    location ^~ /api/resources/v1/ {
        proxy_pass http://127.0.0.1:18085;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Forwarded-Proto https;
        proxy_set_header Connection close;
        client_max_body_size 5m;
        client_body_timeout 30s;
        proxy_request_buffering off;
        proxy_buffering off;
        proxy_connect_timeout 3s;
        proxy_read_timeout 40s;
        proxy_send_timeout 40s;
    }
'''
    return text[:end] + snippet + text[end:]


SCHEMA_CHECK = """SELECT CONCAT_WS(',',
EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema='wgt' AND table_name='file_transfer' AND column_name='idempotency_key'),
EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema='wgt' AND table_name='user' AND column_name='avatar_id'),
EXISTS(SELECT 1 FROM information_schema.columns WHERE table_schema='wgt' AND table_name='user' AND column_name='avatar_version'),
EXISTS(SELECT 1 FROM information_schema.tables WHERE table_schema='wgt' AND table_name='resource_avatar'),
EXISTS(SELECT 1 FROM information_schema.tables WHERE table_schema='wgt' AND table_name='resource_outbox'),
EXISTS(SELECT 1 FROM information_schema.statistics WHERE table_schema='wgt' AND table_name='file_transfer' AND index_name='uk_file_sender_idempotency'));
"""


def make_config(raw, root, image, resource_url, file_mount, config_dir):
    config = copy.deepcopy(raw)
    services = config['services']
    chat = services['chatserver1']
    resource = {
        'image': image, 'restart': 'unless-stopped', 'mem_limit': '768m',
        'command': ['/app/bin/resource_server'],
        'environment': copy.deepcopy(chat.get('environment', {})),
        'volumes': [], 'ports': [{'target': 8085, 'published': '18085', 'host_ip': '127.0.0.1', 'protocol': 'tcp'}],
        'healthcheck': {
            'test': ['CMD', 'python3', '-c', "import urllib.request; urllib.request.urlopen('http://127.0.0.1:8085/health/ready', timeout=3)"],
            'interval': '10s', 'timeout': '4s', 'retries': 6, 'start_period': '30s'},
    }
    for setting in ('networks', 'logging'):
        if setting in chat:
            resource[setting] = copy.deepcopy(chat[setting])
    resource['environment'].update({
        'CHAT_CONFIG_FILE': '/config/resource.ini',
        'CHAT_FILE_STORAGE_ROOT': '/data/files',
        'CHAT_FILE_STORAGE_KEY': '${PROD_FILE_KEY:?preserve the original PROD_FILE_KEY}',
    })
    # Prevent an old global override from changing the explicit single-host config.
    resource['environment'].update({
        'CHAT_RESOURCE_HOST': '0.0.0.0', 'CHAT_RESOURCE_HTTP_PORT': '8085',
        'CHAT_RESOURCE_RPC_PORT': '50065', 'CHAT_RESOURCE_REQUIRE_NFS': 'false',
        'CHAT_GRPC_TLS_MODE': 'mtls', 'CHAT_GRPC_CA_CERT': '/certs/ca.crt',
        'CHAT_GRPC_CERT': '/certs/resource.crt', 'CHAT_GRPC_KEY': '/certs/resource.key',
    })
    resource['volumes'] = [
        {'type': 'bind', 'source': str(config_dir / 'resource.ini'), 'target': '/config/resource.ini', 'read_only': True},
        {'type': 'bind', 'source': str(root / 'run/production/certs'), 'target': '/certs', 'read_only': True},
    ]
    if file_mount['Type'] == 'volume':
        config.setdefault('volumes', {})['migration-resource-files'] = {
            'external': True, 'name': file_mount['Name']}
        resource['volumes'].append({'type': 'volume', 'source': 'migration-resource-files', 'target': '/data/files'})
    else:
        resource['volumes'].append({'type': 'bind', 'source': file_mount['Source'], 'target': '/data/files'})
    for name in ('gate', 'status', 'chatserver1'):
        services[name]['image'] = image
        services[name].pop('build', None)
    chat['volumes'] = [v for v in chat.get('volumes', []) if v.get('target') != '/data/files']
    chat.setdefault('environment', {})['CHAT_RESOURCE_RPC_ENDPOINTS'] = 'resource1:50065'
    for key in ('CHAT_FILE_STORAGE_KEY', 'CHAT_FILE_STORAGE_ROOT'):
        chat['environment'].pop(key, None)
    services['gate'].setdefault('environment', {})['CHAT_RESOURCE_BASE_URL'] = resource_url
    services['resource1'] = resource
    chat.setdefault('depends_on', {})['resource1'] = {'condition': 'service_healthy'}
    return config


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--deploy-dir', required=True, help='Existing server repository/deployment directory')
    parser.add_argument('--image', default='deepecho-server:release-resource-1')
    parser.add_argument('--image-tar', type=Path, help='Load this docker save archive when applying')
    parser.add_argument('--resource-url', default='https://api.deepecho.top/api/resources/v1')
    parser.add_argument('--nginx-site', default='/etc/nginx/sites-available/deepecho')
    parser.add_argument('--apply', action='store_true', help='Perform migration (default: read-only preflight)')
    args = parser.parse_args()
    os.umask(0o077)
    if sys.platform != 'linux' or os.geteuid() != 0:
        raise MigrationError('Run with sudo python3 on the Linux deployment server.')
    if os.uname().machine != 'x86_64':
        raise MigrationError('The supplied build package targets Linux x86_64 only.')
    with contextlib.ExitStack() as cleanup:
        return perform_migration(args, cleanup)


def perform_migration(args, cleanup):
    import fcntl
    root = Path(args.deploy_dir).resolve(strict=True)
    lock = None
    if args.apply:
        lock = cleanup.enter_context((root / '.resource-migration.lock').open('a'))
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as exc:
            raise MigrationError('Another migration is running for this deployment.') from exc
    bundle = Path(__file__).resolve().parent
    compose = root / 'compose.production.yaml'
    envfile = root / '.env.production'
    statefile = root / '.resource-migration-state.json'
    config_dir = root / 'run/resource-migration'
    migration_sql = bundle / '002_resources.sql'
    if not migration_sql.exists():
        migration_sql = bundle.parent.parent / 'database/migrations/002_resources.sql'
    state = None
    if statefile.exists():
        previous = json.loads(statefile.read_text())
        if previous['stage'] == 'complete':
            print('Migration already completed. Backup:', previous['backup'])
            return
        raise MigrationError('A previous migration stopped at ' + previous['stage'] +
                             '. Do not rerun SQL blindly. Recovery information: ' + str(statefile))
    for path in (compose, envfile, bundle / 'resource.ini', bundle / 'generate-resource-cert.sh',
                 migration_sql, root / 'run/production/certs/ca.crt'):
        if not path.is_file():
            raise MigrationError('Required file missing: ' + str(path))
    if compose.is_symlink():
        raise MigrationError('Compose file is a symlink; resolve the custom installation manually.')
    for command in ('docker', 'nginx', 'systemctl', 'openssl', 'du'):
        if not shutil.which(command):
            raise MigrationError('Required command missing: ' + command)
    url = urllib.parse.urlsplit(args.resource_url)
    if (url.scheme != 'https' or not url.hostname or url.username or url.password or
            url.port not in (None, 443) or url.path != '/api/resources/v1' or url.query or url.fragment):
        raise MigrationError('--resource-url must be https://YOUR_API_HOST/api/resources/v1 without a trailing slash.')
    nginx = Path(args.nginx_site).resolve(strict=True)
    old_nginx = nginx.read_text()
    new_nginx = nginx_with_resource(old_nginx, url.hostname)
    run(['nginx', '-t'])
    initial = ['docker', 'compose', '--env-file', envfile, '-f', compose]
    # Discover the actual project from its one running chat container, not directory names.
    ids = run(['docker', 'ps', '-q', '--filter', 'label=com.docker.compose.service=chatserver1']).split()
    candidates = []
    for cid in ids:
        item = json.loads(run(['docker', 'inspect', cid]))[0]
        labels = item['Config'].get('Labels', {})
        if Path(labels.get('com.docker.compose.project.working_dir', '/')).resolve() == root:
            candidates.append(item)
    if len(candidates) != 1:
        raise MigrationError('Expected one running chatserver1 from --deploy-dir. Check the original deployment directory and container status.')
    old_chat = candidates[0]
    project = old_chat['Config']['Labels']['com.docker.compose.project']
    dc = initial + ['-p', project]
    raw = json.loads(run(dc + ['config', '--no-interpolate', '--format', 'json'], cwd=root))
    resolved = json.loads(run(dc + ['config', '--format', 'json'], cwd=root))
    expected = {'mysql', 'redis', 'varify', 'status', 'chatserver1', 'gate'}
    if set(raw['services']) != expected:
        raise MigrationError('This script supports exactly the original six-service single-host deployment. Custom or already-migrated stacks need manual review.')
    # Ensure the supplied file describes the running containers before changing anything.
    containers = {}
    for name in expected:
        cid = run(dc + ['ps', '-q', name], cwd=root).strip()
        if not cid:
            raise MigrationError('Original service is not running: ' + name)
        containers[name] = json.loads(run(['docker', 'inspect', cid]))[0]
    mounts = [m for m in old_chat['Mounts'] if m['Destination'] == '/data/files']
    if len(mounts) != 1 or mounts[0]['Type'] not in ('bind', 'volume') or not mounts[0]['RW']:
        raise MigrationError('Expected the original writable /data/files volume or bind mount.')
    mount = mounts[0]
    source = Path(mount['Source']).resolve(strict=True)
    if root == source or source in root.parents:
        raise MigrationError('Deployment/backup directory cannot be inside the attachment directory.')
    actual_env = dict(v.split('=', 1) for v in old_chat['Config']['Env'] if '=' in v)
    for setting, required in {'CHAT_MYSQL_HOST': 'mysql', 'CHAT_MYSQL_PORT': '3306',
                              'CHAT_MYSQL_SCHEMA': 'wgt', 'CHAT_REDIS_HOST': 'redis',
                              'CHAT_REDIS_PORT': '6379'}.items():
        if actual_env.get(setting) != required:
            raise MigrationError('Unsupported original dependency configuration: ' + setting)
    chat_configs = [m for m in old_chat['Mounts'] if m['Destination'] == actual_env.get('CHAT_CONFIG_FILE')]
    if len(chat_configs) != 1 or chat_configs[0]['Type'] != 'bind':
        raise MigrationError('Cannot inspect the original Chat INI bind mount.')
    ini = configparser.ConfigParser(interpolation=None)
    ini.read(chat_configs[0]['Source'])
    if ini.get('SelfServer', 'Name', fallback='') != 'chatserver1' or ini.get('SelfServer', 'RPCPort', fallback='') != '50055':
        raise MigrationError('Custom Chat node identity/RPC port detected; resource callback configuration needs manual review.')
    if actual_env.get('CHAT_GRPC_TLS_MODE', ini.get('GrpcTLS', 'Mode', fallback='')) != 'mtls':
        raise MigrationError('Original Chat must already use internal mTLS.')
    old_key = actual_env.get('CHAT_FILE_STORAGE_KEY', '')
    if not re.fullmatch(r'[0-9a-fA-F]{64}', old_key):
        raise MigrationError('Cannot establish original file encryption key from the running Chat container.')
    if resolved['services']['chatserver1'].get('environment', {}).get('CHAT_FILE_STORAGE_KEY') != old_key:
        raise MigrationError('Compose file key differs from the running container; restore the original configuration first.')
    cert_mounts = [m for m in old_chat['Mounts'] if m['Destination'] == '/certs']
    if len(cert_mounts) != 1 or Path(cert_mounts[0]['Source']).resolve() != root / 'run/production/certs':
        raise MigrationError('Custom internal certificate mount detected; this script requires run/production/certs.')
    for name in ('mysql', 'redis'):
        target = '/var/lib/mysql' if name == 'mysql' else '/data'
        actual = [m for m in containers[name]['Mounts'] if m['Destination'] == target]
        declared = [m for m in raw['services'][name].get('volumes', []) if m['target'] == target]
        if len(actual) != 1 or len(declared) != 1:
            raise MigrationError('Cannot establish persistent mount for ' + name)
        if actual[0]['Type'] == 'volume':
            volume = declared[0]['source']
            raw.setdefault('volumes', {})[volume] = {'external': True, 'name': actual[0]['Name']}
        elif actual[0]['Type'] == 'bind':
            declared[0]['source'] = actual[0]['Source']
        else:
            raise MigrationError('Unsupported storage for ' + name)
    raw['name'] = project
    mysql = ['docker', 'exec', '-i', containers['mysql']['Id'], 'sh', '-c',
             'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot -N -B wgt']
    schema = run(mysql, input=SCHEMA_CHECK).strip()
    if schema not in ('0,0,0,0,0,0', '1,1,1,1,1,1'):
        raise MigrationError('Partial/unexpected database migration detected: ' + schema)
    cert = root / 'run/production/certs/resource.crt'
    key = root / 'run/production/certs/resource.key'
    if cert.exists() != key.exists():
        raise MigrationError('Only one of resource.crt/resource.key exists; repair the certificate pair first.')
    if cert.exists():
        run(['openssl', 'verify', '-CAfile', root / 'run/production/certs/ca.crt', '-verify_hostname', 'resource1', cert])
        if run(['openssl', 'x509', '-in', cert, '-pubkey', '-noout']) != run(['openssl', 'pkey', '-in', key, '-pubout']):
            raise MigrationError('Resource certificate does not match its private key.')
    elif not (root / 'run/production/ca-private/ca.key').is_file():
        raise MigrationError('Original CA signing key is missing; obtain a Resource certificate from your original CA first.')
    files_bytes = int(run(['du', '-sb', source]).split()[0])
    db_bytes = int(run(mysql, input="SELECT COALESCE(SUM(data_length+index_length),0) FROM information_schema.tables WHERE table_schema='wgt';").strip())
    if shutil.disk_usage(root).free < 2 * (files_bytes + db_bytes) + 1024**3:
        raise MigrationError('Insufficient free backup space (requires twice estimated data size plus 1 GiB).')
    if args.image_tar and not args.image_tar.is_file():
        raise MigrationError('Image archive does not exist: ' + str(args.image_tar))
    print('Preflight passed. Project:', project)
    print('Original file storage:', mount.get('Name', str(source)))
    print('Database:', 'migration required' if schema.startswith('0') else 'already migrated')
    print('Resource URL:', args.resource_url)
    memory_kib = int(re.search(r'MemTotal:\s+(\d+)', Path('/proc/meminfo').read_text()).group(1))
    if memory_kib < 2 * 1024**2:
        print('NOTE: less than 2 GiB RAM. Container limits total about 1696 MiB plus host overhead; check capacity before applying.')
    print('Backup estimate:', (files_bytes + db_bytes) // (1024**2), 'MiB (actual size may differ)')
    if not args.apply:
        print('Read-only check complete. Add --apply to load the image, back up, stop services and migrate.')
        return
    if args.image_tar:
        print('Loading server image before downtime...', flush=True)
        run(['docker', 'load', '-i', args.image_tar])
    run(['docker', 'run', '--rm', '--network', 'none', '--entrypoint', 'python3', args.image,
         '-c', "import os; assert all(os.access('/app/bin/'+n,os.X_OK) for n in ('gate_server','status_server','chat_server','resource_server'))"])
    if shutil.disk_usage(root).free < 2 * (files_bytes + db_bytes) + 1024**3:
        raise MigrationError('Insufficient backup space after loading the image; no services were stopped.')
    stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
    backup = root / 'backups' / ('before-resource-' + stamp)
    backup.mkdir(parents=True, exist_ok=False)
    os.chmod(backup, 0o700)
    state = {'stage': 'preparing', 'backup': str(backup), 'project': project,
             'image': args.image, 'file_storage': mount, 'nginx_site': str(nginx)}

    def stage(value):
        state['stage'] = value
        write_private(statefile, json.dumps(state, indent=2) + '\n')
        print(value, flush=True)

    try:
        stage('backing-up-configuration')
        shutil.copy2(compose, backup / 'compose.production.yaml')
        shutil.copy2(envfile, backup / '.env.production')
        shutil.copytree(root / 'deploy/production', backup / 'deployment-config')
        shutil.copytree(root / 'run/production', backup / 'internal-certificates')
        shutil.copytree('/etc/nginx', backup / 'nginx', symlinks=True)
        (backup / 'original-images.json').write_text(json.dumps({k: {'tag': v['Config']['Image'], 'id': v['Image']} for k, v in containers.items()}, indent=2))
        (backup / 'RECOVERY.txt').write_text(
            'Migration intentionally does not auto-rollback after SQL.\n'
            'Keep services stopped on failure. Do not rerun migration SQL.\n'
            'This folder contains original Compose/env, configs, certificates, Nginx, SQL and files.\n'
            'Rollback requires matching old Compose/configs/client and assessing DB/files compatibility.\n'
            'Never use docker compose down -v. Copy this backup off the server.\n')
        config_dir.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(bundle / 'resource.ini', config_dir / 'resource.ini')
        candidate = root / 'compose.resource-candidate.json'
        candidate_config = make_config(raw, root, args.image, args.resource_url, mount, config_dir)
        write_private(candidate, json.dumps(candidate_config, indent=2) + '\n')
        candidate_dc = ['docker', 'compose', '--env-file', envfile, '-p', project, '-f', candidate]
        checked = json.loads(run(candidate_dc + ['config', '--format', 'json'], cwd=root))
        if checked['services']['resource1']['environment'].get('CHAT_FILE_STORAGE_KEY') != old_key:
            raise MigrationError('PROD_FILE_KEY differs from the existing encryption key. No services stopped.')
        stage('stopping-business-services')
        run(dc + ['stop', 'gate', 'chatserver1', 'status', 'varify'], cwd=root)
        stage('backing-up-database-and-files')
        # Stream SQL directly to a private file; do not buffer the whole database in memory.
        dump = ['docker', 'exec', containers['mysql']['Id'], 'sh', '-c',
                'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysqldump -uroot --single-transaction --routines --triggers --events --no-tablespaces --set-gtid-purged=OFF --databases wgt']
        with (backup / 'wgt.sql').open('wb') as stream:
            dumped = subprocess.run(dump, stdout=stream, stderr=subprocess.PIPE)
        if dumped.returncode or (backup / 'wgt.sql').stat().st_size == 0:
            raise MigrationError('Database backup failed; services remain stopped, original configuration is preserved.')
        shutil.copytree(source, backup / 'files', symlinks=True)
        if not cert.exists():
            run(['sh', bundle / 'generate-resource-cert.sh'], cwd=root)
        if schema == '0,0,0,0,0,0':
            stage('applying-database-migration')
            run(mysql, input=migration_sql.read_text())
        if run(mysql, input=SCHEMA_CHECK).strip() != '1,1,1,1,1,1':
            raise MigrationError('Post-migration schema verification failed.')
        stage('installing-compose-configuration')
        # JSON is valid Compose YAML. Preserve expressions rather than resolved secrets.
        write_private(compose, candidate.read_text())
        candidate.unlink()
        stage('starting-resource-server')
        run(dc + ['up', '-d', '--no-build', '--no-deps', 'resource1'], cwd=root)
        for attempt in range(90):
            cid = run(dc + ['ps', '-a', '-q', 'resource1'], cwd=root).strip()
            resource_state = json.loads(run(['docker', 'inspect', cid]))[0]
            if resource_state['State'].get('Health', {}).get('Status') == 'healthy':
                break
            time.sleep(2)
        else:
            raise MigrationError('Resource readiness timed out. Inspect resource1 logs; old business services remain stopped.')
        stage('installing-nginx-resource-route')
        old_mode = nginx.stat().st_mode & 0o777
        write_private(nginx, new_nginx)
        os.chmod(nginx, old_mode)
        try:
            run(['nginx', '-t'])
        except MigrationError:
            write_private(nginx, old_nginx)
            os.chmod(nginx, old_mode)
            raise
        run(['systemctl', 'reload', 'nginx'])
        stage('starting-updated-business-services')
        run(dc + ['up', '-d', '--no-build', '--no-deps', 'status', 'chatserver1', 'varify', 'gate'], cwd=root)
        business = {}
        for name in ('status', 'chatserver1', 'varify', 'gate'):
            cid = run(dc + ['ps', '-a', '-q', name], cwd=root).strip()
            item = json.loads(run(['docker', 'inspect', cid]))[0]
            business[name] = (cid, item['RestartCount'])
        time.sleep(10)
        for name, (cid, restarts) in business.items():
            item = json.loads(run(['docker', 'inspect', cid]))[0]
            if item['State']['Status'] != 'running' or item['RestartCount'] != restarts:
                raise MigrationError('Updated service stopped or restarted during startup: ' + name)
        try:
            urllib.request.urlopen(args.resource_url + '/users/me/avatar', timeout=15)
        except urllib.error.HTTPError as exc:
            if exc.code != 401:
                raise MigrationError('Public resource URL returned HTTP ' + str(exc.code)) from exc
        else:
            raise MigrationError('Public resource URL did not reject an unauthenticated request with 401.')
        stage('complete')
        print('Migration completed. Backup:', backup)
        print('Original .env.production and encryption key preserved. Install the matching NEW desktop client.')
        print('Verify two-account chat, avatars, new uploads and unexpired historical files. Copy backups off-server.')
    except Exception:
        print('Migration stopped. Last stage:', state['stage'], file=sys.stderr)
        print('Backup/recovery information:', backup, file=sys.stderr)
        print('No volumes were deleted. Do not rerun SQL or remove the state file without reviewing the failure.', file=sys.stderr)
        raise


if __name__ == '__main__':
    try:
        main()
    except (MigrationError, OSError, ValueError, urllib.error.URLError) as error:
        print('ERROR:', error, file=sys.stderr)
        sys.exit(1)
