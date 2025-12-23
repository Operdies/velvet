import sys
import lldb

# If a collection is larger than this, it is most likely not initialized.
# Regardless, the debugger will lag if we try to display it, so mark it as
# suspicious instead,
SUSPICIOUS_SIZE = 1 << 30

def num(obj, name):
    return obj.GetChildMemberWithName(name).GetValueAsUnsigned(0)

def summarize(debugger, type):
    debugger.HandleCommand(f'type summary add -F display.{type}_summary {type}')

def get_string(valobj):
    deref = valobj.GetPointeeData(0, 0xFF)
    error = lldb.SBError()
    strval = deref.GetString(error, 0)
    return strval

def velvet_scene_summary(valobj, internal_dict, options):
    height_val = valobj.GetChildMemberWithName('lines')
    width_val = valobj.GetChildMemberWithName('columns')
    height = height_val.GetValueAsUnsigned(0)
    width = width_val.GetValueAsUnsigned(0)
    return f"mult: [{height}x{width}]"

def configure_velvet_scene(debugger):
    # debugger.HandleCommand('type filter add velvet_scene --child hosts_vec --child prefix --child draw_buffer')
    summarize(debugger, "velvet_scene")

def string_summary(valobj, x, y):
    length_val = valobj.GetChildMemberWithName('len')
    length = length_val.GetValueAsUnsigned(0)
    return f"string[{length}]"

def vec_summary(valobj, x, y):
    raw = valobj.GetNonSyntheticValue()
    prov = vector_SynthProvider(raw, None)
    prov.update()
    return f'vec<{prov.typename}>[{prov.num_elements}]'

def unicode_codepoint_summary(valobj, x, y):
    value = valobj.GetChildMemberWithName('value').GetValueAsUnsigned(0)
    return chr(value)

def screen_cell_style_summary(valobj, x, y):
    bg = valobj.GetChildMemberWithName('bg').GetSummary()
    fg = valobj.GetChildMemberWithName('fg').GetSummary()
    attr = valobj.GetChildMemberWithName('attr').GetSummary()
    return f'{attr}, bg={bg}, fg={fg}'

def screen_cell_summary(valobj, x, y):
    cp = valobj.GetChildMemberWithName('cp');
    value = cp.GetChildMemberWithName('value').GetValueAsUnsigned(0)
    return chr(value)

def color_summary(valobj, x, y):
    cmd = valobj.GetChildMemberWithName('cmd').GetValueAsUnsigned(0)
    table = valobj.GetChildMemberWithName('table').GetValueAsUnsigned(0)
    r = valobj.GetChildMemberWithName('r').GetValueAsUnsigned(0)
    g = valobj.GetChildMemberWithName('g').GetValueAsUnsigned(0)
    b = valobj.GetChildMemberWithName('b').GetValueAsUnsigned(0)

    if cmd == 1:
        return f"#{r:02x}{g:02x}{b:02x}"
    return f"{table}"

def screen_line_summary(valobj, x, y):
    eol = valobj.GetChildMemberWithName('eol').GetValueAsUnsigned(0)
    cells = valobj.GetChildMemberWithName('cells')
    cell_type = cells.GetType().GetPointeeType()
    cell_size = cell_type.GetByteSize()

    summary = f'[n={eol}]'
    for i in range(eol):
        cell = cells.CreateChildAtOffset(
            f'cell[{i}]',
            i * cell_size,
            cell_type
        )
        character = cell.GetChildMemberWithName('cp').GetChildMemberWithName('value').GetValueAsUnsigned(0)
        summary += chr(character)
    return summary

class screen_SynthProvider:
    def __init__(self, o, dict):
        self.o = o

    def num_children(self):
        return len(self.children)

    def add_child(self, index, value):
        self.child_lookup[value.name] = index
        self.children[index] = value

    def get_child_index(self, name):
        if name in self.child_lookup:
            return self.child_lookup[name]
        return -1

    def get_child_at_index(self, index):
        if index < 0:
            return None
        if index >= len(self.children):
            return None
        return self.children[index]

    def update(self):
        self.children = {}
        self.child_lookup = {}
        index = 0
        for chld in self.o.children:
            if chld.name != 'lines':
                self.add_child(index, chld)
                index = index + 1

        lines = self.o.GetChildMemberWithName('lines')
        lines_address = lines.GetValueAsUnsigned(0)
        row_type = lines.GetType().GetPointeeType()
        num_lines = self.o.GetChildMemberWithName('h').GetValueAsUnsigned(0)

        expr = f'*({row_type.name} (*)[{num_lines}])((void*){lines_address})'

        static_lines = self.o.CreateValueFromExpression("lines", expr)
        self.add_child(index, static_lines)

    def has_children(self):
        return True

class string_SynthProvider:
    def __init__(self, o, dict):
        self.o = o

    def num_children(self):
        return len(self.children)

    def add_child(self, value):
        index = self.num_children()
        self.child_lookup[value.name] = index
        self.children[index] = value

    def get_child_index(self, name):
        if name in self.child_lookup:
            return self.child_lookup[name]
        return -1

    def get_child_at_index(self, index):
        if index < 0:
            return None
        if index >= len(self.children):
            return None
        return self.children[index]

    def update(self):
        self.children = {}
        self.child_lookup = {}
        length = self.o.GetChildMemberWithName('len')
        capacity = self.o.GetChildMemberWithName('cap')
        content = self.o.GetChildMemberWithName('content')

        length_num = length.GetValueAsUnsigned(0)
        if length_num < SUSPICIOUS_SIZE:
            contained_type = "uint8_t";
            expr = f'*({contained_type} (*)[{length_num}])((void*){content.GetValueAsUnsigned(0)})'
            elements = self.o.CreateValueFromExpression(f"u8[{length_num}]", expr)

            self.add_child(length)
            if (capacity != None):
                self.add_child(capacity)
            self.add_child(elements)
        else:
            seld.add_child(self.o.CreateValueFromExpression("content", f"<suspicious[{length_num}]>"))

class vector_SynthProvider:
    def __init__(self, o, dict):
        self.o = o

    def num_children(self):
        return len(self.children)

    def add_child(self, value):
        index = self.num_children()
        self.child_lookup[value.name] = index
        self.children[index] = value

    def get_child_index(self, name):
        if name in self.child_lookup:
            return self.child_lookup[name]
        return -1

    def get_child_at_index(self, index):
        if index < 0:
            return None
        if index >= len(self.children):
            return None
        return self.children[index]

    def update(self):
        self.children = {}
        self.child_lookup = {}
        length = self.o.GetChildMemberWithName('length')
        capacity = self.o.GetChildMemberWithName('capacity')
        content = self.o.GetChildMemberWithName('content')
        typename = self.o.GetChildMemberWithName('typename')

        self.num_elements = length.GetValueAsUnsigned(0)

        contained_type = get_string(typename)
        self.typename = contained_type
        expr = f'*({contained_type} (*)[{self.num_elements}])((void*){content.GetValueAsUnsigned(0)})'
        elements = self.o.CreateValueFromExpression("content", expr)

        self.add_child(length)
        self.add_child(capacity)
        self.add_child(elements)

class intslice_SynthProvider:
    def __init__(self, valobj, dict):
        self.valobj = valobj

    def num_children(self):
        return self.length.GetValueAsUnsigned()

    def get_child_index(self, name):
        if name == 'n': return self.num_children() + 1
        logger = lldb.formatters.Logger.Logger()
        try:
            return int(name.lstrip("[").rstrip("]"))
        except:
            return -1

    def get_child_at_index(self, index):
        if index == self.num_children() + 1: return self.length
        slice_type = self.slice.GetType().GetPointeeType()
        if index < 0:
            return None
        if index >= self.num_children():
            return None
        try:
            offset = index * slice_type.size
            return self.slice.CreateChildAtOffset(
                "[" + str(index) + "]", offset, slice_type
            )
        except:
            return None

    def update(self):
        self.length = self.valobj.GetChildMemberWithName("n")
        self.slice = self.valobj.GetChildMemberWithName("content")

    def has_children(self):
        return True

def int_slice_summary(valobj, _1, _2):
    prov = intslice_SynthProvider(valobj, None)
    return "size=" + str(prov.num_children())

def u8_slice_summary(valobj, _1, _2):
    o2 = valobj.GetNonSyntheticValue()
    n = o2.GetChildMemberWithName('len').GetValueAsUnsigned(0)
    ptr = o2.GetChildMemberWithName('content')
    if n > SUSPICIOUS_SIZE:
        return f'<suspicious[{n}]>'

    def escape_control_chars(s: str) -> str:
        table = { }
        for i in range(0x20):
            table[i] = '^' + chr(i+64)
        for i in range(0x80, 0x100):
            table[i] = f"\\x{i:02x}"
        table[0x1b] = '\x1b'
        table[0x0d] = '\\r'
        table[0x0a] = '\\n'
        table[9] = '\\t'
        table[127] = '<DEL>'
        table[8] = '<BS>'

        s = s.translate(table)
        import re
        # replace 5 or more consecutive spaces with <spaces:x>
        s = re.sub(r" {5,}", lambda m: f"<spaces:{len(m.group(0))}>", s)
        # replace remaining spaces with ␠ symbol
        s = re.sub(r" ", lambda m: "␠" * len(m.group(0)), s)
        s = s.replace("\x1b[", " CSI ").replace("\x1b]", " OSC ").replace("\x1b", " ESC ").strip()
        return s


    bytes = ptr.GetPointeeData(0, n).uint8
    summary = ""
    for i in range(n):
        summary += chr(bytes[i])

    summary = escape_control_chars(summary)

    return f'u8[{n}] "{summary}"'

def configure_string(debugger):
    summarize(debugger, "string")

def configure(debugger):
    configure_velvet_scene(debugger)
    configure_string(debugger)
    summarize(debugger, 'int_slice')
    summarize(debugger, "screen_line")
    summarize(debugger, 'u8_slice')
    debugger.HandleCommand(f'type synthetic add int_slice --python-class display.intslice_SynthProvider')
    debugger.HandleCommand(f'type synthetic add vec --python-class display.vector_SynthProvider')
    debugger.HandleCommand(f'type synthetic add string --python-class display.string_SynthProvider')
    debugger.HandleCommand(f'type synthetic add u8_slice --python-class display.string_SynthProvider')
    debugger.HandleCommand(f'type synthetic add screen --python-class display.screen_SynthProvider')
    debugger.HandleCommand( 'type summary add -F display.vec_summary -e -x "^vec$"')
    debugger.HandleCommand( 'type summary add -F display.color_summary -e -x "^color$"')
    debugger.HandleCommand( 'type summary add -F display.unicode_codepoint_summary -e -x "^codepoint$"')
    debugger.HandleCommand( 'type summary add -F display.screen_cell_style_summary -e -x "^screen_cell_style$"')
    debugger.HandleCommand( 'type summary add -F display.screen_cell_summary -e -x "^screen_cell$"')
    debugger.HandleCommand( 'type summary add -F display.screen_line_summary -e -x "^screen_line$"')
def __lldb_init_module(debugger, dict):
    configure(debugger)

