from __future__ import print_function, absolute_import
from .script_interface import ScriptInterfaceHelper, script_interface_register
import numpy as np


@script_interface_register
class Accumulator(ScriptInterfaceHelper):
    """
    Accumulates results from observables.

    Attributes
    ----------
    obs : Instances of :class:`espressomd.observables.Observable`.

    Methods
    -------
    update
        Update the accumulator (get the current values from the observable).
    get_mean
        Returns the samples mean values of the respective observable with which the
        accumulator was initialized.
    get_variance
        Returns the samples variance for the observable.

    """

    _so_name = "Accumulators::Accumulator"
    _so_bind_methods = (
        "update",
        "get_mean",
        "get_variance"
    )
    _so_creation_policy = "LOCAL"


@script_interface_register
class AutoUpdateAccumulators(ScriptInterfaceHelper):
    _so_name = "Accumulators::AutoUpdateAccumulators"
    _so_creation_policy = "LOCAL"

    def add(self, *args, **kwargs):
        if len(args) == 1:
            if isinstance(args[0], Accumulator):
                accumulator = args[0]
            else:
                raise TypeError(
                    "Either a Accumulator object or key-value pairs for the parameters of a Accumulator object need to be passed.")
        else:
            accumulator = Accumulator(**kwargs)
        self.call_method("add", object=accumulator)
        return accumulator

    def remove(self, Accumulator):
        self.call_method("remove", object=Accumulator)
