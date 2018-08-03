#!/usr/bin/env python2
# Copyright 2017 The Dawn Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

############################################################
# COMMON
############################################################
from collections import namedtuple

class Name:
    def __init__(self, name, native=False):
        self.native = native
        if native:
            self.chunks = [name]
        else:
            self.chunks = name.split(' ')

    def CamelChunk(self, chunk):
        return chunk[0].upper() + chunk[1:]

    def canonical_case(self):
        return (' '.join(self.chunks)).lower()

    def concatcase(self):
        return ''.join(self.chunks)

    def camelCase(self):
        return self.chunks[0] + ''.join([self.CamelChunk(chunk) for chunk in self.chunks[1:]])

    def CamelCase(self):
        return ''.join([self.CamelChunk(chunk) for chunk in self.chunks])

    def SNAKE_CASE(self):
        return '_'.join([chunk.upper() for chunk in self.chunks])

    def snake_case(self):
        return '_'.join(self.chunks)

class Type:
    def __init__(self, name, record, native=False):
        self.record = record
        self.dict_name = name
        self.name = Name(name, native=native)
        self.category = record['category']
        self.is_builder = self.name.canonical_case().endswith(" builder")

EnumValue = namedtuple('EnumValue', ['name', 'value'])
class EnumType(Type):
    def __init__(self, name, record):
        Type.__init__(self, name, record)
        self.values = [EnumValue(Name(m['name']), m['value']) for m in self.record['values']]

BitmaskValue = namedtuple('BitmaskValue', ['name', 'value'])
class BitmaskType(Type):
    def __init__(self, name, record):
        Type.__init__(self, name, record)
        self.values = [BitmaskValue(Name(m['name']), m['value']) for m in self.record['values']]
        self.full_mask = 0
        for value in self.values:
            self.full_mask = self.full_mask | value.value

class NativeType(Type):
    def __init__(self, name, record):
        Type.__init__(self, name, record, native=True)

class NativelyDefined(Type):
    def __init__(self, name, record):
        Type.__init__(self, name, record)

class MethodArgument:
    def __init__(self, name, typ, annotation):
        self.name = name
        self.type = typ
        self.annotation = annotation
        self.length = None

Method = namedtuple('Method', ['name', 'return_type', 'arguments'])
class ObjectType(Type):
    def __init__(self, name, record):
        Type.__init__(self, name, record)
        self.methods = []
        self.native_methods = []
        self.built_type = None

class StructureMember:
    def __init__(self, name, typ, annotation):
        self.name = name
        self.type = typ
        self.annotation = annotation
        self.length = None

class StructureType(Type):
    def __init__(self, name, record):
        Type.__init__(self, name, record)
        self.extensible = record.get("extensible", False)
        self.members = []

############################################################
# PARSE
############################################################
import json

def is_native_method(method):
    return method.return_type.category == "natively defined" or \
        any([arg.type.category == "natively defined" for arg in method.arguments])

def link_object(obj, types):
    def make_method(record):
        arguments = []
        arguments_by_name = {}
        for a in record.get('args', []):
            arg = MethodArgument(Name(a['name']), types[a['type']], a.get('annotation', 'value'))
            arguments.append(arg)
            arguments_by_name[arg.name.canonical_case()] = arg

        for (arg, a) in zip(arguments, record.get('args', [])):
            if arg.annotation != 'value':
                if not 'length' in a:
                    if arg.type.category == 'structure':
                        arg.length = "constant"
                        arg.constant_length = 1
                    else:
                        assert(false)
                elif a['length'] == 'strlen':
                    arg.length = 'strlen'
                else:
                    arg.length = arguments_by_name[a['length']]

        return Method(Name(record['name']), types[record.get('returns', 'void')], arguments)

    methods = [make_method(m) for m in obj.record.get('methods', [])]
    obj.methods = [method for method in methods if not is_native_method(method)]
    obj.native_methods = [method for method in methods if is_native_method(method)]

    # Compute the built object type for builders
    if obj.is_builder:
        for method in obj.methods:
            if method.name.canonical_case() == "get result":
                obj.built_type = method.return_type
                break
        assert(obj.built_type != None)

def link_structure(struct, types):
    def make_member(m):
        return StructureMember(Name(m['name']), types[m['type']], m.get('annotation', 'value'))

    members = []
    members_by_name = {}
    for m in struct.record['members']:
        member = make_member(m)
        members.append(member)
        members_by_name[member.name.canonical_case()] = member
    struct.members = members

    for (member, m) in zip(members, struct.record['members']):
        # TODO(kainino@chromium.org): More robust pointer/length handling?
        if 'length' in m:
            member.length = members_by_name[m['length']]

def parse_json(json):
    category_to_parser = {
        'bitmask': BitmaskType,
        'enum': EnumType,
        'native': NativeType,
        'natively defined': NativelyDefined,
        'object': ObjectType,
        'structure': StructureType,
    }

    types = {}

    by_category = {}
    for name in category_to_parser.keys():
        by_category[name] = []

    for (name, record) in json.items():
        if name[0] == '_':
            continue
        category = record['category']
        parsed = category_to_parser[category](name, record)
        by_category[category].append(parsed)
        types[name] = parsed

    for obj in by_category['object']:
        link_object(obj, types)

    for struct in by_category['structure']:
        link_structure(struct, types)

    for category in by_category.keys():
        by_category[category] = sorted(by_category[category], key=lambda typ: typ.name.canonical_case())

    return {
        'types': types,
        'by_category': by_category
    }

#############################################################
# OUTPUT
#############################################################
import re, os, sys
from collections import OrderedDict

try:
    import jinja2
except ImportError:
    # Try using Chromium's Jinja2
    dir, _ = os.path.split(os.path.realpath(__file__))
    third_party_dir = os.path.normpath(dir + (os.path.sep + os.path.pardir) * 2)
    sys.path.insert(1, third_party_dir)
    import jinja2

# A custom Jinja2 template loader that removes the extra indentation
# of the template blocks so that the output is correctly indented
class PreprocessingLoader(jinja2.BaseLoader):
    def __init__(self, path):
        self.path = path

    def get_source(self, environment, template):
        path = os.path.join(self.path, template)
        if not os.path.exists(path):
            raise jinja2.TemplateNotFound(template)
        mtime = os.path.getmtime(path)
        with open(path) as f:
            source = self.preprocess(f.read())
        return source, path, lambda: mtime == os.path.getmtime(path)

    blockstart = re.compile('{%-?\s*(if|for|block)[^}]*%}')
    blockend = re.compile('{%-?\s*end(if|for|block)[^}]*%}')

    def preprocess(self, source):
        lines = source.split('\n')

        # Compute the current indentation level of the template blocks and remove their indentation
        result = []
        indentation_level = 0

        for line in lines:
            # The capture in the regex adds one element per block start or end so we divide by two
            # there is also an extra line chunk corresponding to the line end, so we substract it.
            numends = (len(self.blockend.split(line)) - 1) // 2
            indentation_level -= numends

            line = self.remove_indentation(line, indentation_level)

            # Manually perform the lstrip_blocks jinja2 env options as it available starting from 2.7
            # and Travis only has Jinja 2.6
            if line.lstrip().startswith('{%'):
                line = line.lstrip()

            result.append(line)

            numstarts = (len(self.blockstart.split(line)) - 1) // 2
            indentation_level += numstarts

        return '\n'.join(result) + '\n'

    def remove_indentation(self, line, n):
        for _ in range(n):
            if line.startswith(' '):
                line = line[4:]
            elif line.startswith('\t'):
                line = line[1:]
            else:
                assert(line.strip() == '')
        return line

FileRender = namedtuple('FileRender', ['template', 'output', 'params_dicts'])

def do_renders(renders, template_dir, output_dir):
    env = jinja2.Environment(loader=PreprocessingLoader(template_dir), trim_blocks=True, line_comment_prefix='//*')
    for render in renders:
        params = {}
        for param_dict in render.params_dicts:
            params.update(param_dict)
        output = env.get_template(render.template).render(**params)

        output_file = output_dir + os.path.sep + render.output
        directory = os.path.dirname(output_file)
        if not os.path.exists(directory):
            os.makedirs(directory)

        with open(output_file, 'w') as outfile:
            outfile.write(output)

#############################################################
# MAIN SOMETHING WHATEVER
#############################################################
import argparse, sys

def as_varName(*names):
    return names[0].camelCase() + ''.join([name.CamelCase() for name in names[1:]])

def as_cType(name):
    if name.native:
        return name.concatcase()
    else:
        return 'dawn' + name.CamelCase()

def as_cppType(name):
    if name.native:
        return name.concatcase()
    else:
        return name.CamelCase()

def decorate(name, typ, arg):
    if arg.annotation == 'value':
        return typ + ' ' + name
    elif arg.annotation == '*':
        return typ + '* ' + name
    elif arg.annotation == 'const*':
        return typ + ' const * ' + name
    else:
        assert(False)

def annotated(typ, arg):
    name = as_varName(arg.name)
    return decorate(name, typ, arg)

def as_cEnum(type_name, value_name):
    assert(not type_name.native and not value_name.native)
    return 'DAWN' + '_' + type_name.SNAKE_CASE() + '_' + value_name.SNAKE_CASE()

def as_cppEnum(value_name):
    assert(not value_name.native)
    if value_name.concatcase()[0].isdigit():
        return "e" + value_name.CamelCase()
    return value_name.CamelCase()

def as_cMethod(type_name, method_name):
    assert(not type_name.native and not method_name.native)
    return 'dawn' + type_name.CamelCase() + method_name.CamelCase()

def as_MethodSuffix(type_name, method_name):
    assert(not type_name.native and not method_name.native)
    return type_name.CamelCase() + method_name.CamelCase()

def as_cProc(type_name, method_name):
    assert(not type_name.native and not method_name.native)
    return 'dawn' + 'Proc' + type_name.CamelCase() + method_name.CamelCase()

def as_frontendType(typ):
    if typ.category == 'object':
        if typ.is_builder:
            return typ.name.CamelCase() + '*'
        else:
            return typ.name.CamelCase() + 'Base*'
    elif typ.category in ['bitmask', 'enum']:
        return 'dawn::' + typ.name.CamelCase()
    elif typ.category == 'structure':
        return as_cppType(typ.name)
    else:
        return as_cType(typ.name)

def cpp_native_methods(types, typ):
    methods = typ.methods + typ.native_methods

    if typ.is_builder:
        methods.append(Method(Name('set error callback'), types['void'], [
            MethodArgument(Name('callback'), types['builder error callback'], 'value'),
            MethodArgument(Name('userdata1'), types['callback userdata'], 'value'),
            MethodArgument(Name('userdata2'), types['callback userdata'], 'value'),
        ]))

    return methods

def c_native_methods(types, typ):
    return cpp_native_methods(types, typ) + [
        Method(Name('reference'), types['void'], []),
        Method(Name('release'), types['void'], []),
    ]

def js_native_methods(types, typ):
    return cpp_native_methods(types, typ)

def debug(text):
    print(text)

def main():
    targets = ['dawn_headers', 'libdawn', 'mock_dawn', 'dawn_wire', "dawn_native_utils"]

    parser = argparse.ArgumentParser(
        description = 'Generates code for various target for Dawn.',
        formatter_class = argparse.ArgumentDefaultsHelpFormatter
    )
    parser.add_argument('json', metavar='DAWN_JSON', nargs=1, type=str, help ='The DAWN JSON definition to use.')
    parser.add_argument('-t', '--template-dir', default='templates', type=str, help='Directory with template files.')
    parser.add_argument('-o', '--output-dir', default=None, type=str, help='Output directory for the generated source files.')
    parser.add_argument('-T', '--targets', default=None, type=str, help='Comma-separated subset of targets to output. Available targets: ' + ', '.join(targets))
    parser.add_argument('--print-dependencies', action='store_true', help='Prints a space separated list of file dependencies, used for CMake integration')
    parser.add_argument('--print-outputs', action='store_true', help='Prints a space separated list of file outputs, used for CMake integration')
    parser.add_argument('--gn', action='store_true', help='Make the printing of dependencies/outputs GN-friendly')

    args = parser.parse_args()

    if args.targets != None:
        targets = args.targets.split(',')

    with open(args.json[0]) as f:
        loaded_json = json.loads(f.read())

    # A fake api_params to avoid parsing the JSON when just querying dependencies and outputs
    api_params = {
        'types': {}
    }
    if not args.print_outputs and not args.print_dependencies:
        api_params = parse_json(loaded_json)

    base_params = {
        'enumerate': enumerate,
        'format': format,
        'len': len,
        'debug': debug,

        'Name': lambda name: Name(name),

        'as_annotated_cType': lambda arg: annotated(as_cType(arg.type.name), arg),
        'as_annotated_cppType': lambda arg: annotated(as_cppType(arg.type.name), arg),
        'as_cEnum': as_cEnum,
        'as_cppEnum': as_cppEnum,
        'as_cMethod': as_cMethod,
        'as_MethodSuffix': as_MethodSuffix,
        'as_cProc': as_cProc,
        'as_cType': as_cType,
        'as_cppType': as_cppType,
        'as_varName': as_varName,
        'decorate': decorate,
    }

    renders = []

    c_params = {'native_methods': lambda typ: c_native_methods(api_params['types'], typ)}
    cpp_params = {'native_methods': lambda typ: cpp_native_methods(api_params['types'], typ)}

    if 'dawn_headers' in targets:
        renders.append(FileRender('api.h', 'dawn/dawn.h', [base_params, api_params, c_params]))
        renders.append(FileRender('apicpp.h', 'dawn/dawncpp.h', [base_params, api_params, cpp_params]))
        renders.append(FileRender('apicpp_traits.h', 'dawn/dawncpp_traits.h', [base_params, api_params, cpp_params]))

    if 'libdawn' in targets:
        additional_params = {'native_methods': lambda typ: cpp_native_methods(api_params['types'], typ)}
        renders.append(FileRender('api.c', 'dawn/dawn.c', [base_params, api_params, c_params]))
        renders.append(FileRender('apicpp.cpp', 'dawn/dawncpp.cpp', [base_params, api_params, cpp_params]))

    if 'mock_dawn' in targets:
        renders.append(FileRender('mock_api.h', 'mock/mock_dawn.h', [base_params, api_params, c_params]))
        renders.append(FileRender('mock_api.cpp', 'mock/mock_dawn.cpp', [base_params, api_params, c_params]))

    if 'dawn_native_utils' in targets:
        frontend_params = [
            base_params,
            api_params,
            c_params,
            {
                'as_frontendType': lambda typ: as_frontendType(typ), # TODO as_frontendType and friends take a Type and not a Name :(
                'as_annotated_frontendType': lambda arg: annotated(as_frontendType(arg.type), arg)
            }
        ]

        renders.append(FileRender('dawn_native/ValidationUtils.h', 'dawn_native/ValidationUtils_autogen.h', frontend_params))
        renders.append(FileRender('dawn_native/ValidationUtils.cpp', 'dawn_native/ValidationUtils_autogen.cpp', frontend_params))
        renders.append(FileRender('dawn_native/api_structs.h', 'dawn_native/dawn_structs_autogen.h', frontend_params))
        renders.append(FileRender('dawn_native/api_structs.cpp', 'dawn_native/dawn_structs_autogen.cpp', frontend_params))
        renders.append(FileRender('dawn_native/ProcTable.cpp', 'dawn_native/ProcTable.cpp', frontend_params))

    if 'dawn_wire' in targets:
        wire_params = [
            base_params,
            api_params,
            c_params,
            {
                'as_wireType': lambda typ: typ.name.CamelCase() + '*' if typ.category == 'object' else as_cppType(typ.name)
            }
        ]
        renders.append(FileRender('dawn_wire/WireCmd.h', 'dawn_wire/WireCmd_autogen.h', wire_params))
        renders.append(FileRender('dawn_wire/WireCmd.cpp', 'dawn_wire/WireCmd_autogen.cpp', wire_params))
        renders.append(FileRender('dawn_wire/WireClient.cpp', 'dawn_wire/WireClient.cpp', wire_params))
        renders.append(FileRender('dawn_wire/WireServer.cpp', 'dawn_wire/WireServer.cpp', wire_params))

    output_separator = '\n' if args.gn else ';'
    if args.print_dependencies:
        dependencies = set(
            [os.path.abspath(args.template_dir + os.path.sep + render.template) for render in renders] +
            [os.path.abspath(args.json[0])] +
            [os.path.realpath(__file__)]
        )
        dependencies = [dependency.replace('\\', '/') for dependency in dependencies]
        sys.stdout.write(output_separator.join(dependencies))
        return 0

    if args.print_outputs:
        outputs = set(
            [os.path.abspath(args.output_dir + os.path.sep + render.output) for render in renders]
        )
        outputs = [output.replace('\\', '/') for output in outputs]
        sys.stdout.write(output_separator.join(outputs))
        return 0

    do_renders(renders, args.template_dir, args.output_dir)

if __name__ == '__main__':
    sys.exit(main())
