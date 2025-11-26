import sys
import lldb

def num(obj, name):
    return obj.GetChildMemberWithName(name).GetValueAsUnsigned(0)

def summarize(debugger, type):
    debugger.HandleCommand(f'type summary add -F display.{type}_summary {type}')

def multiplexer_summary(valobj, internal_dict, options):
    height_val = valobj.GetChildMemberWithName('rows')
    width_val = valobj.GetChildMemberWithName('columns')
    height = height_val.GetValueAsUnsigned(0)
    width = width_val.GetValueAsUnsigned(0)
    return f"mult: [{height}x{width}]"

def configure_multiplexer(debugger):
    # debugger.HandleCommand('type filter add multiplexer --child hosts_vec --child prefix --child draw_buffer')
    summarize(debugger, "multiplexer")

def string_summary(valobj, x, y):
    length_val = valobj.GetChildMemberWithName('len')
    length = length_val.GetValueAsUnsigned(0)
    return f"string[{length}]"

def vec_summary(valobj, x, y):
    raw = valobj.GetNonSyntheticValue()
    prov = vector_SynthProvider(raw, None)
    prov.update()
    return f'{prov.get_typename()}[{prov.num_children()}]'

class vector_SynthProvider:
    def __init__(self, o, dict):
        self.o = o

    def num_children(self):
        return self.length.GetValueAsUnsigned(0)

    def get_child_index(self, name):
        try:
            return int(name.lstrip("[").rstrip("]"))
        except:
            return -1

    def get_child_at_index(self, index):
        if index < 0:
            return None
        if index >= self.num_children():
            return None
        try:
            offset = index * self.element_size.GetValueAsUnsigned(0)
            return self.content.CreateChildAtOffset(
                "[" + str(index) + "]", offset, self.element_type
            )
        except:
            return None

    def get_typename(self):
        return self.typename

    def update(self):
        try:
            self.length = self.o.GetChildMemberWithName("length")
            self.content = self.o.GetChildMemberWithName("content")
            self.element_size = self.o.GetChildMemberWithName("element_size")
            self.typename = self.o.GetChildMemberWithName('typename').GetSummary().strip('"')
            self.element_type = self.o.target.FindFirstType(self.typename)
        except e:
            print(f'error updating vector: {e}')

    def has_children(self):
        return True

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
            offset = index * 4
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


def configure_string(debugger):
    summarize(debugger, "string")

def configure(debugger):
    configure_multiplexer(debugger)
    configure_string(debugger)
    summarize(debugger, 'int_slice')
    debugger.HandleCommand(f'type synthetic add int_slice --python-class display.intslice_SynthProvider')
    debugger.HandleCommand(f'type synthetic add vec --python-class display.vector_SynthProvider')
    # debugger.HandleCommand('type summary add -F display.vec_summary vec')
    debugger.HandleCommand(
        'type summary add -F display.vec_summary -e -x "^vec$"'
    )
    # type synthetic add Foo --python-class Foo_Tools.Foo_Provider


def __lldb_init_module(debugger, dict):
    configure(debugger)

