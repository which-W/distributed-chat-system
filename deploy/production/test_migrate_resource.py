"""Offline migration regression checks. External services are simulated, not deployed."""
import copy
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch
import urllib.error

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('migration', HERE / 'migrate-resource.py')
migration = importlib.util.module_from_spec(spec)
spec.loader.exec_module(migration)

NGINX = '''server { listen 80; server_name api.example.test; }
server {
    listen 443 ssl;
    server_name api.example.test;
    # comment with a fake closing brace }
    location / { proxy_pass http://127.0.0.1:18080; }
}
'''


class ConfigTests(unittest.TestCase):
    def test_nginx_preserves_original_api_and_adds_only_one_resource_route(self):
        result = migration.nginx_with_resource(NGINX, 'api.example.test')
        self.assertIn('proxy_pass http://127.0.0.1:18080;', result)
        self.assertEqual(result.count('location ^~ /api/resources/v1/'), 1)
        with self.assertRaises(migration.MigrationError):
            migration.nginx_with_resource(result, 'api.example.test')

    def test_unknown_or_ambiguous_nginx_site_is_rejected(self):
        for text, hostname in [(NGINX, 'wrong.test'), (NGINX + NGINX, 'api.example.test')]:
            with self.assertRaises(migration.MigrationError):
                migration.nginx_with_resource(text, hostname)

    def test_local_persistence_reuses_actual_volume_or_bind_and_drops_chat_writer(self):
        for mount in [{'Type': 'volume', 'Name': 'actual-old-project-files'},
                      {'Type': 'bind', 'Source': '/srv/my-existing-files'}]:
            original = {'services': {name: {'image': 'old', 'environment': {}}
                                    for name in ('gate', 'status', 'chatserver1')}}
            original['services']['chatserver1'].update({
                'volumes': [{'target': '/data/files', 'source': 'old-files'}],
                'environment': {'CHAT_FILE_STORAGE_KEY': '${PROD_FILE_KEY}', 'CHAT_PEER_RPC_TOKEN': '${PROD_PEER_TOKEN}'},
                'networks': {'private': None}})
            preserved = copy.deepcopy(original)
            result = migration.make_config(original, Path('/srv/app'), 'new-image',
                                           'https://api.example.test/api/resources/v1', mount, Path('/srv/app/run/migration'))
            self.assertEqual(original, preserved)
            resource = result['services']['resource1']
            self.assertEqual(resource['networks'], {'private': None})
            self.assertEqual(resource['ports'][0]['host_ip'], '127.0.0.1')
            self.assertEqual(result['services']['chatserver1']['volumes'], [])
            self.assertNotIn('CHAT_FILE_STORAGE_KEY', result['services']['chatserver1']['environment'])
            self.assertIn('${PROD_FILE_KEY', resource['environment']['CHAT_FILE_STORAGE_KEY'])
            if mount['Type'] == 'volume':
                self.assertEqual(result['volumes']['migration-resource-files'], {'external': True, 'name': mount['Name']})
            else:
                self.assertEqual(resource['volumes'][-1]['source'], mount['Source'])


class WorkflowTests(unittest.TestCase):
    def setUp(self):
        temp_root = HERE.parent.parent / 'build/migration-tests'
        temp_root.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=temp_root)
        self.base = Path(self.temp.name).resolve()
        self.root = self.base / 'app'
        self.root.mkdir()
        (self.root / 'deploy/production').mkdir(parents=True)
        (self.root / 'run/production/certs').mkdir(parents=True)
        (self.root / 'run/production/ca-private').mkdir()
        (self.root / 'run/production/certs/ca.crt').write_text('test CA')
        (self.root / 'run/production/ca-private/ca.key').write_text('test signing key')
        self.chat_ini = self.root / 'deploy/production/chat.ini'
        self.chat_ini.write_text('[SelfServer]\nName=chatserver1\nRPCPort=50055\n[GrpcTLS]\nMode=mtls\n')
        self.files = self.base / 'existing-files'
        self.files.mkdir()
        (self.files / 'old-attachment').write_bytes(b'existing encrypted bytes')
        self.nginx = self.base / 'nginx-site'
        self.nginx.write_text(NGINX)
        self.fake_nginx = self.base / 'nginx-config'
        self.fake_nginx.mkdir()
        (self.fake_nginx / 'nginx.conf').write_text(NGINX)
        self.old_env = 'PROD_FILE_KEY=' + 'a' * 64 + '\n'
        (self.root / '.env.production').write_text(self.old_env)
        self.compose = self.root / 'compose.production.yaml'
        self.compose.write_text('original compose, preserved until migration\n')
        self.schema = '0,0,0,0,0,0'
        self.calls = []
        self.fail_dump = False
        self.fail_nginx = False
        self.config = {'name': 'actual-project', 'services': {name: {'image': 'old-image', 'environment': {}}
                         for name in ('mysql', 'redis', 'varify', 'status', 'chatserver1', 'gate')},
                       'volumes': {'old-files': {'name': 'actual-files'}, 'mysql-data': {}, 'redis-data': {}}}
        self.environment = {
            'CHAT_MYSQL_HOST': 'mysql', 'CHAT_MYSQL_PORT': '3306', 'CHAT_MYSQL_SCHEMA': 'wgt',
            'CHAT_REDIS_HOST': 'redis', 'CHAT_REDIS_PORT': '6379', 'CHAT_CONFIG_FILE': '/config/chat.ini',
            'CHAT_FILE_STORAGE_KEY': 'a' * 64,
        }
        self.config['services']['chatserver1'].update({
            'environment': self.environment.copy(),
            'volumes': [{'type': 'volume', 'source': 'old-files', 'target': '/data/files'}]})
        self.config['services']['mysql']['volumes'] = [{'type': 'volume', 'source': 'mysql-data', 'target': '/var/lib/mysql'}]
        self.config['services']['redis']['volumes'] = [{'type': 'volume', 'source': 'redis-data', 'target': '/data'}]
        self.containers = {}
        for name in self.config['services']:
            self.containers[name] = {'Id': name, 'Image': 'sha256:old', 'Config': {'Image': 'old-image',
                'Env': [k + '=' + v for k, v in self.environment.items()],
                'Labels': {'com.docker.compose.project': 'actual-project', 'com.docker.compose.project.working_dir': str(self.root)}}, 'Mounts': []}
            self.containers[name].update({'RestartCount': 0, 'State': {'Status': 'running'}})
        self.containers['chatserver1']['Mounts'] = [
            {'Type': 'volume', 'Name': 'actual-files', 'Source': str(self.files), 'Destination': '/data/files', 'RW': True},
            {'Type': 'bind', 'Source': str(self.root / 'run/production/certs'), 'Destination': '/certs'},
            {'Type': 'bind', 'Source': str(self.chat_ini), 'Destination': '/config/chat.ini'}]
        for name, target in [('mysql', '/var/lib/mysql'), ('redis', '/data')]:
            self.containers[name]['Mounts'] = [{'Type': 'volume', 'Name': 'actual-' + name, 'Destination': target}]

    def tearDown(self):
        self.temp.cleanup()

    def run_command(self, args, **kwargs):
        args = list(map(str, args))
        self.calls.append((args, kwargs.get('input')))
        if args[:2] == ['docker', 'ps']:
            return 'chatserver1\n'
        if args[:2] == ['docker', 'inspect']:
            if args[2] == 'resource1':
                return json.dumps([{'State': {'Health': {'Status': 'healthy'}}}])
            return json.dumps([self.containers[args[2]]])
        if args[:2] == ['docker', 'compose']:
            if 'config' in args:
                path = Path(args[args.index('-f') + 1])
                cfg = copy.deepcopy(self.config) if path == self.compose else json.loads(path.read_text())
                if 'resource1' in cfg['services']:
                    cfg['services']['resource1']['environment']['CHAT_FILE_STORAGE_KEY'] = 'a' * 64
                return json.dumps(cfg)
            if 'ps' in args:
                return args[-1] + '\n'
            return ''
        if args[:2] == ['docker', 'exec']:
            sql = kwargs['input']
            if sql == migration.SCHEMA_CHECK:
                return self.schema + '\n'
            if 'COALESCE(SUM' in sql:
                return '128\n'
            if 'ALTER TABLE file_transfer' in sql:
                self.schema = '1,1,1,1,1,1'
                return ''
            self.fail('Unexpected SQL: ' + sql)
        if args[0] == 'du':
            return '1024 files\n'
        if args[0] == 'sh':
            (self.root / 'run/production/certs/resource.crt').write_text('test cert')
            (self.root / 'run/production/certs/resource.key').write_text('test key')
        if args[:2] == ['nginx', '-t'] and self.fail_nginx and '/api/resources/v1/' in self.nginx.read_text():
            raise migration.MigrationError('simulated nginx test failure')
        return ''

    def execute(self, apply=False):
        arguments = ['migrate-resource.py', '--deploy-dir', str(self.root), '--resource-url',
                     'https://api.example.test/api/resources/v1', '--nginx-site', str(self.nginx)]
        if apply:
            arguments.append('--apply')
        original_read = Path.read_text
        original_copy = migration.shutil.copytree

        def read(path, *a, **kw):
            if str(path).replace('\\', '/') == '/proc/meminfo':
                return 'MemTotal: 4194304 kB\n'
            return original_read(path, *a, **kw)

        def copytree(source, dest, *args, **kwargs):
            if str(source) == '/etc/nginx':
                source = self.fake_nginx
            return original_copy(source, dest, *args, **kwargs)

        def dump(args, stdout, **kwargs):
            stdout.write(b'-- MySQL dump\n-- Dump completed on test\n')
            return types.SimpleNamespace(returncode=int(self.fail_dump))

        fake_fcntl = types.SimpleNamespace(LOCK_EX=1, LOCK_NB=2, flock=lambda *args: None)
        with patch.object(sys, 'argv', arguments), patch.object(sys, 'platform', 'linux'), \
                patch.object(migration.os, 'geteuid', return_value=0, create=True), \
                patch.object(migration.os, 'uname', return_value=types.SimpleNamespace(machine='x86_64'), create=True), \
                patch.dict(sys.modules, {'fcntl': fake_fcntl}), \
                patch.object(migration, 'run', side_effect=self.run_command), \
                patch.object(migration.shutil, 'which', return_value='/usr/bin/tool'), \
                patch.object(migration.shutil, 'disk_usage', return_value=types.SimpleNamespace(free=100 * 1024**3)), \
                patch.object(migration.shutil, 'copytree', side_effect=copytree), \
                patch.object(Path, 'read_text', read), \
                patch.object(migration.subprocess, 'run', side_effect=dump), \
                patch.object(migration.time, 'sleep'), \
                patch.object(migration.urllib.request, 'urlopen', side_effect=urllib.error.HTTPError('https://example', 401, '', {}, None)):
            migration.main()

    def test_preflight_never_stops_services_or_writes_state(self):
        self.execute()
        self.assertFalse((self.root / '.resource-migration-state.json').exists())
        self.assertFalse(any('stop' in args or 'up' in args for args, _ in self.calls))

    def test_partial_schema_stops_before_mutation(self):
        self.schema = '1,0,0,0,0,0'
        with self.assertRaisesRegex(migration.MigrationError, 'Partial'):
            self.execute(apply=True)
        self.assertFalse(any('stop' in args for args, _ in self.calls))

    def test_backup_failure_never_migrates_or_replaces_compose(self):
        self.fail_dump = True
        with self.assertRaisesRegex(migration.MigrationError, 'backup failed'):
            self.execute(apply=True)
        self.assertEqual(self.schema, '0,0,0,0,0,0')
        self.assertTrue(self.compose.read_text().startswith('original compose'))
        self.assertFalse(any('up' in args for args, _ in self.calls))

    def test_complete_migration_preserves_keys_files_and_project(self):
        self.execute(apply=True)
        state = json.loads((self.root / '.resource-migration-state.json').read_text())
        self.assertEqual(state['stage'], 'complete')
        self.assertEqual((self.root / '.env.production').read_text(), self.old_env)
        self.assertEqual((Path(state['backup']) / 'files/old-attachment').read_bytes(), b'existing encrypted bytes')
        final = json.loads(self.compose.read_text())
        self.assertEqual(final['name'], 'actual-project')
        self.assertEqual(final['volumes']['migration-resource-files']['name'], 'actual-files')
        self.assertEqual(final['volumes']['mysql-data']['name'], 'actual-mysql')
        self.assertNotIn('a' * 64, json.dumps(final['services']['resource1']))
        previous_calls = len(self.calls)
        self.execute(apply=True)
        self.assertEqual(len(self.calls), previous_calls)

    def test_fully_migrated_schema_skips_sql(self):
        self.schema = '1,1,1,1,1,1'
        self.execute(apply=True)
        self.assertFalse(any(sql and 'ALTER TABLE file_transfer' in sql for _, sql in self.calls))

    def test_nginx_failure_restores_original_site(self):
        self.fail_nginx = True
        with self.assertRaisesRegex(migration.MigrationError, 'nginx test failure'):
            self.execute(apply=True)
        self.assertEqual(self.nginx.read_text(), NGINX)
        self.assertFalse(any('up' in args and 'gate' in args for args, _ in self.calls))


if __name__ == '__main__':
    unittest.main()
